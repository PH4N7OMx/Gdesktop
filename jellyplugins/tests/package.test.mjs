import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { validateManifest, pack } from "../scripts/pack.mjs";

const manifest = { apiVersion: 1, id: "jelly.test", name: "Test", version: "1.0.0", permissions: {} };

test("each Telegram capability requires a bounded chat ID scope", () => {
  for (const scope of ["joinChannels", "botChats", "attachmentChats", "editChats", "reactionChats", "historyChats"]) {
    assert.doesNotThrow(() => validateManifest({ ...manifest, permissions: { [scope]: ["-1001234567890"] } }));
    for (const value of [true, ["*"], [123], ["https://t.me/example"], Array(33).fill("123")])
      assert.throws(() => validateManifest({ ...manifest, permissions: { [scope]: value } }));
  }
});

test("unknown, wildcard, local and malformed permission scopes are rejected", () => {
  for (const permissions of [
    { exec: true }, { readChats: ["*"] }, { sendChats: [123] },
    { menuChats: ["123\n"] }, { readChats: ["123\n"] }, { httpHosts: ["example.com\n"] },
    { httpHosts: ["127.0.0.1"] }, { httpHosts: ["*.example.com"] },
    { httpHosts: ["router.local"] }, { httpHosts: ["https://example.com"] },
    { maxMessagesPerHour: 0 }, { maxMessagesPerHour: 1000 }, { storage: "yes" },
  ]) assert.throws(() => validateManifest({ ...manifest, permissions }));
  assert.throws(() => validateManifest({ ...manifest, id: "../outside" }));
  assert.throws(() => validateManifest({ ...manifest, apiVersion: 2 }));
});

test("bundled packages carry no external runtime imports or async syntax", async () => {
  const folder = await mkdtemp(join(tmpdir(), "jelly-plugin-test-"));
  await writeFile(join(folder, "manifest.json"), JSON.stringify(manifest));
  await writeFile(join(folder, "index.ts"), "export default async jelly => { await jelly.storage.get('x'); };");
  const file = await pack(folder);
  const parsed = JSON.parse(await readFile(file, "utf8"));
  assert.equal(parsed.manifest.id, "jelly.test");
  assert.ok(parsed.code.includes("JellyPlugin"));
  assert.doesNotMatch(parsed.code, /\basync\s+(?:function|\()/);
});

test("system imports cannot enter a package", async () => {
  const folder = await mkdtemp(join(tmpdir(), "jelly-plugin-test-"));
  await writeFile(join(folder, "manifest.json"), JSON.stringify(manifest));
  await writeFile(join(folder, "index.ts"), "import fs from 'node:fs'; export default () => fs.readFileSync('/x');");
  await assert.rejects(() => pack(folder), /Unavailable plugin import/);
});


test("file and balance grants are booleans and Mini Apps require positive bot IDs", () => {
  for (const key of ["fileRead", "fileWrite", "moneyRead"]) {
    for (const value of [true, false]) assert.doesNotThrow(() => validateManifest({ ...manifest, permissions: { [key]: value } }));
    for (const value of [1, "yes", [], null]) assert.throws(() => validateManifest({ ...manifest, permissions: { [key]: value } }));
  }
  assert.doesNotThrow(() => validateManifest({ ...manifest, permissions: { webviewBots: ["123456789", "9007199254740993"] } }));
  for (const value of [["-1001234567890"], ["0"], ["*"], [123], ["00123"], Array(33).fill("123")])
    assert.throws(() => validateManifest({ ...manifest, permissions: { webviewBots: value } }));
});


test("settings forms reject duplicate keys, mismatched defaults and excessive content", () => {
  const boolean = { key: "enabled", label: "Enabled", type: "boolean", default: true };
  const number = { key: "amount", label: "Amount", type: "number", default: 5, min: 1, max: 10 };
  const text = { key: "prefix", label: "Prefix", type: "string", default: "ok", maxLength: 20 };
  const select = { key: "mode", label: "Mode", type: "select", default: "A", options: ["A", "B"] };
  assert.doesNotThrow(() => validateManifest({ ...manifest, permissions: { menuChats: ["123"], uiDialogs: true }, settingsSchema: [boolean, number, text, select] }));
  for (const schema of [null, {}, [boolean, boolean], Array(17).fill(boolean),
    [{ ...boolean, default: "true" }], [{ ...number, default: 11 }], [{ ...number, min: 20 }],
    [{ ...number, default: Infinity }], [{ ...number, min: -1e10 }],
    [{ ...text, maxLength: 1 }], [{ ...text, maxLength: 1.5 }], [{ ...text, default: "x".repeat(3000) }],
    [{ ...select, default: "C" }], [{ ...select, options: ["A", "A"] }], [{ ...select, options: Array(9).fill("A") }],
    [{ ...boolean, key: "enabled\n" }], [{ ...boolean, key: "__proto__" }], [{ ...boolean, key: 1 }],
    [{ ...boolean, label: "" }], [{ ...boolean, surprise: true }], [{ ...boolean, options: [] }],
    [{ ...boolean, type: "customNativeView" }],
  ]) assert.throws(() => validateManifest({ ...manifest, settingsSchema: schema }));
  for (const permissions of [{ uiDialogs: "yes" }, { menuChats: ["*"] }, { menuChats: [123] }, { menuChats: Array(33).fill("123") }])
    assert.throws(() => validateManifest({ ...manifest, permissions }));
});
