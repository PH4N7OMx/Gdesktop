import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { contract, validatePackage, validateParams } from "../contract.mjs";
import { pack } from "../pack.mjs";

const manifest = { id: "test.plugin", name: "Test", version: "2.0.0", apiVersion: 2, capabilities: [] };
const pkg = () => ({ formatVersion: 2, manifest: { ...manifest }, code: "export default () => {};" });

test("new package versions are independent and legacy containers are rejected", () => {
  assert.equal(validatePackage(pkg()).manifest.apiVersion, 2);
  for (const value of [
    { manifest, code: "old" }, { ...pkg(), formatVersion: 1 },
    { ...pkg(), manifest: { ...manifest, apiVersion: 1 } },
    { ...pkg(), manifest: { ...manifest, permissions: { sendChats: ["-123"] } } },
    { ...pkg(), extra: true }, { ...pkg(), code: "\0" },
  ]) assert.throws(() => validatePackage(value));
});

test("manifest identity, capabilities and package size are validated", () => {
  for (const override of [
    { id: "../other" }, { name: " " }, { id: "test.plugin\n" },
    { capabilities: ["native.shell"] }, { capabilities: ["storage", "storage"] },
    { version: "2.0.0-beta" }, { author: null },
  ]) assert.throws(() => validatePackage({ ...pkg(), manifest: { ...manifest, ...override } }));
  assert.throws(() => validatePackage({ ...pkg(), code: "x".repeat(contract.limits.packageBytes) }));
});

test("actual parameters are canonicalized and unknown fields are never silently permitted", () => {
  assert.deepEqual(validateParams("telegram.sendText", { chatId: "-123", text: "test" }),
    { chatId: "-123", text: "test", silent: false });
  assert.deepEqual(validateParams("telegram.readHistory", { chatId: "123" }), { chatId: "123", limit: 20 });
  for (const [method, params] of [
    ["telegram.sendText", { chatId: "-123", text: "test", replyTo: "1" }],
    ["telegram.sendText", { chatId: "-123\n", text: "test" }],
    ["telegram.sendText", { chatId: -123, text: "test" }],
    ["telegram.sendText", { chatId: "-123", text: "" }],
    ["telegram.readHistory", { chatId: "123", limit: 101 }],
    ["schedule.every", { id: "a", seconds: 59 }],
    ["schedule.every", { id: "a", seconds: 60.5 }],
    ["storage.set", { key: "a", value: undefined }],
    ["__proto__", {}], ["native.exec", {}],
  ]) assert.throws(() => validateParams(method, params));
});

test("own storage values are copied within their budget", () => {
  const value = { list: [1, "test"] };
  const params = validateParams("storage.set", { key: "data", value });
  value.list.push(2);
  assert.deepEqual(params.value, { list: [1, "test"] });
  assert.throws(() => validateParams("storage.set", { key: "data", value: "ы".repeat(40000) }));
  for (const item of [NaN, Infinity, { missing: undefined }, { callback() {} }, [undefined], new Date(), new Map()]) {
    assert.throws(() => validateParams("storage.set", { key: "data", value: item }));
  }
  const cyclic = {};
  cyclic.self = cyclic;
  assert.throws(() => validateParams("storage.set", { key: "data", value: cyclic }));
  const deep = Array.from({ length: 40 }).reduce(value => [value], null);
  assert.throws(() => validateParams("storage.set", { key: "data", value: deep }));
  const special = JSON.parse('{"__proto__":{"visible":true}}');
  assert.equal(Object.hasOwn(validateParams("storage.set", { key: "data", value: special }).value, "__proto__"), true);
});

test("the packer creates a new ESM container without any v1 wrapper", async () => {
  const directory = await mkdtemp(join(tmpdir(), "jelly-v2-pack-"));
  try {
    const path = await pack(fileURLToPath(new URL("../examples/hello", import.meta.url)), join(directory, "hello.jelly"));
    const result = validatePackage(JSON.parse(await readFile(path, "utf8")));
    assert.equal(result.formatVersion, 2);
    assert.match(result.code, /export\s*\{/);
    assert.doesNotMatch(result.code, /JellyPlugin\.default|var JellyPlugin/);
  } finally { await rm(directory, { recursive: true, force: true }); }
});

test("external runtime dependencies and unbundled imports cannot enter packages", async () => {
  const directory = await mkdtemp(join(tmpdir(), "jelly-v2-import-"));
  try {
    await writeFile(join(directory, "manifest.json"), JSON.stringify(manifest));
    for (const source of [
      'import fs from "node:fs"; export default () => fs.readFileSync("x");',
      'export default async () => import("https://example.com/code.js");',
      'export default async () => { const source = "unknown"; return import(source); };',
    ]) {
      await writeFile(join(directory, "index.ts"), source);
      await assert.rejects(() => pack(directory));
    }
  } finally { await rm(directory, { recursive: true, force: true }); }
});
