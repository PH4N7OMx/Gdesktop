import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { validateManifest, pack } from "../scripts/pack.mjs";

const manifest = { apiVersion: 1, id: "jelly.test", name: "Test", version: "1.0.0", permissions: {} };

test("unknown, wildcard, local and malformed permission scopes are rejected", () => {
  for (const permissions of [
    { exec: true }, { readChats: ["*"] }, { sendChats: [123] },
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
