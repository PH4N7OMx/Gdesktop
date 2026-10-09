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
  for (const key of ["telegram", "storage", "http", "timers", "ui"]) assert.ok(Object.isFrozen(api[key]));
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
