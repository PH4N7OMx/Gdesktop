import test from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import { readFile, mkdtemp } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { pack } from "../scripts/pack.mjs";

const runtime = await readFile(new URL(
  "../sdk/runtime.js", import.meta.url), "utf8");

async function plugin(name, settings = {}) {
  const directory = await mkdtemp(join(tmpdir(), "jelly-example-test-"));
  const packagePath = await pack(fileURLToPath(new URL(`../examples/${name}`, import.meta.url)),
    join(directory, "example.jelly"));
  const { code } = JSON.parse(await readFile(packagePath, "utf8"));
  const frames = [], storage = new Map(), sent = [], timers = new Map();
  const state = { xml: "", sendError: null };
  const context = vm.createContext({ settings, __jellyNative: {
    post(text) { frames.push(JSON.parse(text)); },
  } });
  vm.runInContext(runtime, context);
  vm.runInContext(code, context);
  vm.runInContext("__jellyStart(JellyPlugin.default, settings)", context);
  let consumed = 0;
  const drain = async () => {
    for (let step = 0; step < 100; step++) {
      await new Promise(resolve => setImmediate(resolve));
      if (consumed === frames.length) return;
      while (consumed < frames.length) {
        const frame = frames[consumed++];
        if (frame.type !== "request") continue;
        const params = frame.params;
        let value = true, error = "", retryAfter = 0;
        if (frame.method === "storage.get") value = storage.get(params.key) ?? null;
        else if (frame.method === "storage.set") storage.set(params.key, params.value);
        else if (frame.method === "timers.every") timers.set(params.name, params.seconds);
        else if (frame.method === "http.get") value = { status: 200, text: state.xml };
        else if (frame.method === "telegram.sendMessage") {
          if (state.sendError) { error = state.sendError; retryAfter = 60; }
          else { sent.push(params); value = { accepted: true, messageId: sent.length }; }
        }
        context.frame = { type: "result", id: frame.id, value, error, retryAfter };
        vm.runInContext("__jellyDispatch(frame)", context);
      }
    }
    throw new Error("Example did not settle");
  };
  await drain();
  return { frames, storage, sent, timers, state, async event(event, data) {
    context.frame = { type: "event", event, data };
    vm.runInContext("__jellyDispatch(frame)", context);
    await drain();
  } };
}

test("hello saves its counter across button presses", async () => {
  const env = await plugin("hello");
  await env.event("action", { id: "hello" });
  await env.event("action", { id: "hello" });
  assert.equal(env.storage.get("count"), 2);
  assert.equal(env.sent.length, 0);
});

const rss = ids => `<rss><channel>${ids.map(id =>
  `<item><guid>${id}</guid><title>Post ${id}</title><link>https://example.com/${id}</link></item>`)
  .join("")}</channel></rss>`;

test("feed ignores the initial backlog and only sends a new entry once", async () => {
  const env = await plugin("feed-to-chat", {
    url: "https://example.com/feed.xml", chatId: "123", dryRun: false,
  });
  env.state.xml = rss(["old"]);
  await env.event("action", { id: "poll" });
  assert.equal(env.sent.length, 0);
  env.state.xml = rss(["new", "old"]);
  await env.event("action", { id: "poll" });
  await env.event("action", { id: "poll" });
  assert.equal(env.sent.length, 1);
  assert.equal(env.sent[0].chatId, "123");
  assert.ok(env.sent[0].text.includes("Post new"));
});

test("feed preview sends nothing and does not consume the entry", async () => {
  const env = await plugin("feed-to-chat", {
    url: "https://example.com/feed.xml", chatId: "123", dryRun: true,
  });
  env.state.xml = rss(["old"]);
  await env.event("action", { id: "poll" });
  env.state.xml = rss(["new", "old"]);
  await env.event("action", { id: "poll" });
  assert.equal(env.sent.length, 0);
  assert.ok(!env.storage.get("seen").includes("new"));
  assert.ok(env.frames.some(frame => frame.type === "log" && frame.text.includes("Предпросмотр")));
});

test("FloodWait preserves unsent entries and pauses repeat attempts", async () => {
  const env = await plugin("feed-to-chat", {
    url: "https://example.com/feed.xml", chatId: "123", dryRun: false,
  });
  env.state.xml = rss(["old"]);
  await env.event("action", { id: "poll" });
  env.state.xml = rss(["new", "old"]);
  env.state.sendError = "FLOOD_WAIT";
  await env.event("action", { id: "poll" });
  await env.event("action", { id: "poll" });
  assert.ok(!env.storage.get("seen").includes("new"));
  assert.equal(env.frames.filter(frame => frame.method === "telegram.sendMessage").length, 1);
});

test("giveaway watcher deduplicates discoveries and performs no participation actions", async () => {
  const env = await plugin("giveaway-watch");
  const message = { chatId: "-100123", messageId: 10, text: "Розыгрыш", date: 123 };
  await env.event("message.new", message);
  await env.event("message.new", message);
  await env.event("message.new", { ...message, messageId: 11, text: "Обычный текст" });
  assert.equal(env.storage.get("giveaways").length, 1);
  assert.equal(env.sent.length, 0);
  assert.ok(!env.frames.some(frame => frame.method?.startsWith("http.")));
});

test("concurrent giveaway discoveries are saved without lost entries", async () => {
  const env = await plugin("giveaway-watch");
  await Promise.all([10, 11, 10, 12].map(messageId => env.event("message.new", {
    chatId: "-100123", messageId, text: "Giveaway", date: 123,
  })));
  assert.equal(env.storage.get("giveaways").length, 3);
});
