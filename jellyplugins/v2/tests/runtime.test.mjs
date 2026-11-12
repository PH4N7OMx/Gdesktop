import test from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

const header = await readFile(new URL("../host/bootstrap.generated.h", import.meta.url), "utf8");
const literals = Array.from(header.matchAll(/R"JELLY_BOOTSTRAP\(([\s\S]*?)\)JELLY_BOOTSTRAP"/g), match => match[1]);
const bootstrap = literals.join("");
const identity = { protocolVersion: 2, instanceId: "test-instance", generation: 1 };
const pkg = { formatVersion: 2, manifest: { id: "test.plugin", name: "Test", version: "2.0.0",
  apiVersion: 2, capabilities: [] }, code: "export default () => {};" };
const flush = () => new Promise(resolve => setImmediate(resolve));

test("embedded source stays within native compiler string limits", () => {
  assert.ok(literals.length > 1);
  assert.ok(literals.every(literal => Buffer.byteLength(literal) <= 16380));
  assert.ok(Buffer.byteLength(bootstrap) < 65535);
});

function environment() {
  const frames = [];
  const context = vm.createContext({});
  const factory = vm.runInContext(bootstrap, context, { timeout: 1000 });
  const runtime = factory(text => frames.push(JSON.parse(text)));
  runtime.initialize(JSON.stringify({ ...identity, type: "init", package: pkg, settings: {} }));
  const activate = source => runtime.activate(vm.runInContext(`(${source})`, context));
  const dispatch = frame => runtime.dispatch(JSON.stringify({ ...identity, ...frame }));
  return { runtime, frames, context, activate, dispatch };
}

test("the native bootstrap waits for asynchronous activation", async () => {
  const env = environment();
  env.activate("async api => { await api.storage.get('startup'); }");
  await flush();
  assert.equal(env.runtime.state(), "activating");
  assert.equal(env.frames.length, 1);
  assert.equal(env.frames[0].method, "storage.get");
  env.dispatch({ type: "result", requestId: 1, value: null });
  await flush();
  assert.equal(env.runtime.state(), "ready");
  assert.equal(env.frames.at(-1).type, "ready");
});

test("activation errors never emit ready", async () => {
  const env = environment();
  env.activate("async () => { throw new Error('activation failed'); }");
  await flush();
  assert.equal(env.runtime.state(), "failed");
  assert.equal(env.frames.some(frame => frame.type === "ready"), false);
  assert.equal(env.frames.at(-1).error.message, "activation failed");
});

test("a thrown object cannot break error reporting through getters", async () => {
  const env = environment();
  env.activate("() => { throw { get message() { throw new Error('getter'); } }; }");
  await flush();
  assert.equal(env.runtime.state(), "failed");
  assert.equal(env.frames.at(-1).error.message, "Plugin execution failed");
});

test("plugin code cannot alter transport or runtime intrinsics", async () => {
  const env = environment();
  env.activate(`api => {
    if (!Object.isFrozen(api) || !Object.isFrozen(api.telegram)) throw Error('mutable API');
    if (globalThis.__jellyNative || globalThis.runtimeExports || globalThis.__jellyDispatch) throw Error('transport exposed');
    try { Promise.prototype.then = () => {}; } catch {}
    try { Map.prototype.get = () => ({}); } catch {}
    try { globalThis.JSON = { stringify: () => '{}' }; } catch {}
    api.log.info('isolated');
  }`);
  await flush();
  assert.equal(env.runtime.state(), "ready");
  assert.equal(env.frames.find(frame => frame.type === "log").message, "isolated");
});

test("out of order replies match requests and copied parameters cannot change", async () => {
  const env = environment();
  env.activate(`async api => {
    const params = { chatId: '-123', text: 'first' };
    const a = api.telegram.sendText(params);
    params.text = 'changed';
    const b = api.storage.get('b');
    const values = await Promise.all([a, b]);
    api.log.info(JSON.stringify(values));
  }`);
  await flush();
  assert.equal(env.frames[0].params.text, "first");
  assert.equal(env.frames[0].params.silent, false);
  env.dispatch({ type: "result", requestId: 2, value: "second" });
  env.dispatch({ type: "result", requestId: 1, value: { messageId: "1" } });
  await flush();
  assert.equal(env.runtime.state(), "ready");
  assert.equal(env.frames.find(frame => frame.type === "log").message, '[{"messageId":"1"},"second"]');
});

test("stale generations and malformed frames fail before resolving requests", async () => {
  const env = environment();
  env.activate("async api => { await api.storage.get('startup'); }");
  await flush();
  for (const frame of [
    { type: "result", generation: 2, requestId: 1, value: null },
    { type: "result", instanceId: "other", requestId: 1, value: null },
    { type: "result", requestId: 1, value: null, error: {} },
    { type: "result", requestId: 1, error: null },
    { type: "result", requestId: 1.2, value: null },
    { type: "result", requestId: 1, error: { code: 1, message: "bad", stage: "api" } },
  ]) assert.throws(() => env.dispatch(frame));
  assert.equal(env.runtime.state(), "activating");
  env.dispatch({ type: "result", requestId: 1, value: null });
  await flush();
  assert.equal(env.runtime.state(), "ready");
});

test("request queue is bounded and structured errors retain retryAfter", async () => {
  const env = environment();
  env.activate(`async api => {
    const pending = [];
    for (let i = 0; i < 33; i++) pending.push(api.storage.get(String(i)).catch(error => api.log.info(error.code)));
    await Promise.all(pending);
  }`);
  await flush();
  assert.equal(env.frames.filter(frame => frame.type === "request").length, 32);
  assert.ok(env.frames.some(frame => frame.message === "REQUEST_LIMIT"));
  for (let requestId = 1; requestId <= 32; requestId++) {
    env.dispatch({ type: "result", requestId, error: { code: "FLOOD_WAIT", message: "Wait", stage: "api", retryAfter: 90 } });
  }
  await flush();
  assert.equal(env.runtime.state(), "ready");
  const second = environment();
  second.activate(`async api => {
    try { await api.storage.get('a'); } catch (error) { api.log.info(error.retryAfter + ':' + error.stage); }
  }`);
  await flush();
  second.dispatch({ type: "result", requestId: 1, error: { code: "FLOOD_WAIT", message: "Wait", stage: "api", retryAfter: 90 } });
  await flush();
  assert.ok(second.frames.some(frame => frame.message === "90:api"));
});

test("scheduled handlers do not overlap and skipped events are acknowledged", async () => {
  const env = environment();
  env.activate(`async api => {
    await api.schedule.every('check', 60, async () => { await api.storage.get('tick'); });
  }`);
  await flush();
  assert.deepEqual(env.frames[0].params, { id: "check", seconds: 60, first: "after-period" });
  env.dispatch({ type: "result", requestId: 1, value: true });
  await flush();
  env.dispatch({ type: "event", eventId: 1, event: "schedule", scheduleId: "check" });
  await flush();
  env.dispatch({ type: "event", eventId: 2, event: "schedule", scheduleId: "check" });
  await flush();
  assert.equal(env.frames.filter(frame => frame.method === "storage.get").length, 1);
  assert.ok(env.frames.some(frame => frame.type === "eventDone" && frame.eventId === 2 && frame.outcome === "skipped"));
  env.dispatch({ type: "result", requestId: 2, value: null });
  await flush();
  assert.ok(env.frames.some(frame => frame.type === "eventDone" && frame.eventId === 1 && frame.outcome === "completed"));
  assert.throws(() => env.dispatch({ type: "event", eventId: 2, event: "schedule", scheduleId: "check" }));
});

test("failed schedule registration removes its handler and permits retry", async () => {
  const env = environment();
  env.activate(`async api => {
    try { await api.schedule.every('check', 60, () => {}); } catch {}
    await api.schedule.every('check', 60, () => {});
  }`);
  await flush();
  env.dispatch({ type: "result", requestId: 1, error: { code: "DENIED", message: "Denied", stage: "policy" } });
  await flush();
  assert.equal(env.frames[1].method, "schedule.every");
  env.dispatch({ type: "result", requestId: 2, value: true });
  await flush();
  assert.equal(env.runtime.state(), "ready");
});

test("stop cancels pending activation and awaits cleanup without sending new actions", async () => {
  const env = environment();
  env.activate("async api => { await api.storage.get('startup'); }");
  await flush();
  env.dispatch({ type: "stop" });
  await flush();
  assert.equal(env.runtime.state(), "stopped");
  assert.equal(env.frames.filter(frame => frame.type === "ready").length, 0);
  const active = environment();
  active.activate("api => async () => { try { await api.storage.get('cleanup'); } catch {} }");
  await flush();
  active.dispatch({ type: "stop" });
  await flush();
  assert.equal(active.runtime.state(), "stopped");
  assert.equal(active.frames.filter(frame => frame.type === "request").length, 0);
});

test("the packaged hello example activates through the embedded bootstrap", async () => {
  const entry = new URL("../examples/hello/index.ts", import.meta.url);
  const bundled = await build({ entryPoints: [fileURLToPath(entry)],
    bundle: true, write: false, format: "iife", globalName: "entry", platform: "neutral", target: "es2020" });
  const env = environment();
  const activate = vm.runInContext(`(() => { ${bundled.outputFiles[0].text}; return entry.default; })()`, env.context);
  env.runtime.activate(activate);
  await flush();
  assert.equal(env.runtime.state(), "ready");
  assert.equal(env.frames.find(frame => frame.type === "log").message, "JellyPlugins v2 activated");
});
