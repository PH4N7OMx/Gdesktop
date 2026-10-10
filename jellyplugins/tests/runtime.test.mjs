import test from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import { readFile } from "node:fs/promises";

const source = await readFile(new URL(
  "../sdk/runtime.js", import.meta.url), "utf8");

async function environment(activate) {
  const frames = [];
  const context = vm.createContext({ __jellyNative: {
    post(text) { frames.push(JSON.parse(text)); },
  } });
  vm.runInContext(source, context);
  context.activate = activate;
  vm.runInContext("__jellyStart(activate, {})", context);
  await new Promise(resolve => setImmediate(resolve));
  return { context, frames, async dispatch(frame) {
    context.frame = frame;
    vm.runInContext("__jellyDispatch(frame)", context);
    await new Promise(resolve => setImmediate(resolve));
  } };
}

test("API has no native object and is immutable", async () => {
  let api;
  const env = await environment(jelly => { api = jelly; });
  assert.equal(vm.runInContext("typeof __jellyNative", env.context), "undefined");
  assert.ok(Object.isFrozen(api));
  for (const key of ["telegram", "storage", "http", "timers", "ui", "files", "money", "miniApps"]) assert.ok(Object.isFrozen(api[key]));
  assert.equal(api.exec, undefined);
  assert.equal(api.fs, undefined);
});

test("out of order responses are matched to the correct request", async () => {
  let first, second;
  const env = await environment(jelly => {
    jelly.storage.get("first").then(value => { first = value; });
    jelly.storage.get("second").then(value => { second = value; });
  });
  await env.dispatch({ type: "result", id: 2, value: "B" });
  await env.dispatch({ type: "result", id: 1, value: "A" });
  assert.equal(first, "A");
  assert.equal(second, "B");
});

test("permission denial and FloodWait remain structured errors", async () => {
  let caught;
  const env = await environment(jelly => {
    jelly.telegram.sendMessage("123", "test").catch(error => { caught = error; });
  });
  await env.dispatch({ type: "result", id: 1, error: "FLOOD_WAIT", retryAfter: 90 });
  assert.equal(caught.code, "FLOOD_WAIT");
  assert.equal(caught.retryAfter, 90);
  assert.equal(caught.message, "FLOOD_WAIT");
});

test("unsubscription removes handlers and handler failures are logged", async () => {
  let count = 0;
  const env = await environment(jelly => {
    const off = jelly.on("timer", () => { count++; });
    off();
    jelly.on("timer", () => { throw new Error("handler failed"); });
  });
  await env.dispatch({ type: "event", event: "timer", data: { name: "poll" } });
  assert.equal(count, 0);
  assert.ok(env.frames.some(frame => frame.type === "log" && frame.text.includes("handler failed")));
});

test("activation failure stops startup and pending request count is bounded", async () => {
  const failed = await environment(() => { throw new Error("startup failed"); }).catch(error => error);
  assert.match(String(failed), /startup failed/);
  let denied;
  const env = await environment(jelly => {
    for (let i = 0; i < 33; i++) jelly.storage.get(String(i)).catch(error => { denied = error; });
  });
  assert.equal(env.frames.filter(frame => frame.type === "request").length, 32);
  assert.equal(denied.message, "TOO_MANY_REQUESTS");
  assert.equal(denied.code, "TOO_MANY_REQUESTS");
  assert.equal(denied.retryAfter, 0);
});

test("asynchronous activation failure produces a fatal frame", async () => {
  const env = await environment(() => Promise.reject(new Error("async startup failed")));
  assert.ok(env.frames.some(frame => frame.type === "fatal" && frame.error.includes("async startup failed")));
});

test("extended Telegram methods preserve IDs and structured failures", async () => {
  let api;
  const env = await environment(jelly => { api = jelly; });
  const outcomes = [
    api.telegram.joinChannel("-1001234567890"),
    api.telegram.getHistory("-1001234567890", { beforeId: 50, limit: 10, chatId: "forged" }),
    api.telegram.clickButton("-1001234567890", 42, 0, 1),
    api.telegram.sendAttachment("123", "-1001234567890", 42, { silent: true, chatId: "forged" }),
    api.telegram.editMessage("123", 43, "Updated"),
    api.telegram.setReaction("123", 43, "👍"),
  ].map(promise => promise.catch(error => ({ error: error.code, retryAfter: error.retryAfter })));
  assert.deepEqual(env.frames.map(frame => frame.method), [
    "telegram.joinChannel", "telegram.getHistory", "telegram.clickButton",
    "telegram.sendAttachment", "telegram.editMessage", "telegram.setReaction",
  ]);
  assert.deepEqual(env.frames[1].params, { chatId: "-1001234567890", beforeId: 50, limit: 10 });
  assert.deepEqual(env.frames[3].params, { chatId: "123", sourceChatId: "-1001234567890", messageId: 42, silent: true });
  await env.dispatch({ type: "result", id: 6, error: "FLOOD_WAIT", retryAfter: 60 });
  await env.dispatch({ type: "result", id: 5, error: "PERMISSION_DENIED" });
  await env.dispatch({ type: "result", id: 4, value: { accepted: true, messageId: 99 } });
  await env.dispatch({ type: "result", id: 3, value: { text: "Done", alert: false } });
  await env.dispatch({ type: "result", id: 2, value: [{ messageId: 42, buttons: [] }] });
  await env.dispatch({ type: "result", id: 1, value: true });
  assert.deepEqual(await Promise.all(outcomes), [
    true, [{ messageId: 42, buttons: [] }], { text: "Done", alert: false },
    { accepted: true, messageId: 99 }, { error: "PERMISSION_DENIED", retryAfter: 0 },
    { error: "FLOOD_WAIT", retryAfter: 60 },
  ]);
});


test("selected files, balances and Mini Apps preserve consent errors and exact amounts", async () => {
  let api;
  const env = await environment(jelly => { api = jelly; });
  const outcomes = [api.files.readText(), api.files.writeText("hello"),
    api.money.getBalance(), api.money.getBalance("ton"),
    api.miniApps.open("123", { startParam: "test", botId: "forged" })]
    .map(p => p.catch(e => ({ code: e.code, retryAfter: e.retryAfter })));
  assert.deepEqual(env.frames.map(f => [f.method, f.params]), [
    ["files.readText", {}], ["files.writeText", { text: "hello", suggestedName: "export.txt" }],
    ["money.getBalance", { currency: "stars" }], ["money.getBalance", { currency: "ton" }],
    ["miniApps.open", { startParam: "test", botId: "123" }],
  ]);
  await env.dispatch({ type: "result", id: 5, value: { requested: true } });
  await env.dispatch({ type: "result", id: 4, error: "PERMISSION_DENIED" });
  await env.dispatch({ type: "result", id: 3, value: { currency: "stars", whole: "9007199254740993", nanos: 123 } });
  await env.dispatch({ type: "result", id: 2, value: { name: "chosen.txt" } });
  await env.dispatch({ type: "result", id: 1, error: "USER_CANCELLED" });
  assert.deepEqual(await Promise.all(outcomes), [
    { code: "USER_CANCELLED", retryAfter: 0 }, { name: "chosen.txt" },
    { currency: "stars", whole: "9007199254740993", nanos: 123 },
    { code: "PERMISSION_DENIED", retryAfter: 0 }, { requested: true },
  ]);
});


test("binary file API uses base64 with user-selected output names", async () => {
  let api;
  const env = await environment(jelly => { api = jelly; });
  const read = api.files.readFile();
  const write = api.files.writeFile("AAEC/w==", "image.bin");
  assert.deepEqual(env.frames.map(f => [f.method, f.params]), [
    ["files.readFile", {}], ["files.writeFile", { base64: "AAEC/w==", suggestedName: "image.bin" }],
  ]);
  await env.dispatch({ type: "result", id: 1, value: { name: "selected.bin", base64: "AAEC/w==" } });
  await env.dispatch({ type: "result", id: 2, value: { name: "user-renamed.bin" } });
  assert.deepEqual(await read, { name: "selected.bin", base64: "AAEC/w==" });
  assert.deepEqual(await write, { name: "user-renamed.bin" });
});
