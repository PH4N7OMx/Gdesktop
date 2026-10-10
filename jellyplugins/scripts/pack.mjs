import { build } from "esbuild";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { resolve, join } from "node:path";
import { pathToFileURL } from "node:url";

const allowedPermissions = new Set([
  "readChats", "sendChats", "httpHosts", "joinChannels", "botChats", "attachmentChats", "editChats", "reactionChats", "historyChats", "storage", "timers", "ui", "maxMessagesPerHour", "fileRead", "fileWrite", "moneyRead", "webviewBots",
]);

export function validateManifest(manifest) {
  if (!manifest || manifest.apiVersion !== 1 || !/^[a-z][a-z0-9.-]{2,63}$/.test(manifest.id)
    || typeof manifest.name !== "string" || !manifest.name || manifest.name.length > 80
    || !/^\d+\.\d+\.\d+$/.test(manifest.version)
    || !manifest.permissions || Array.isArray(manifest.permissions)
    || typeof manifest.permissions !== "object") throw new Error("Invalid manifest");
  for (const [key, value] of Object.entries(manifest.permissions)) {
    if (!allowedPermissions.has(key)) throw new Error(`Unknown permission: ${key}`);
    if (["readChats", "sendChats", "httpHosts", "joinChannels", "botChats", "attachmentChats", "editChats", "reactionChats", "historyChats", "webviewBots"].includes(key)) {
      if (!Array.isArray(value) || value.length > 32) throw new Error(`Invalid scope: ${key}`);
      const pattern = key === "httpHosts"
        ? /^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z]{2,63}$/
        : key === "webviewBots" ? /^[1-9][0-9]{0,15}$/ : /^-?[1-9][0-9]{0,15}$/;
      if (value.some(item => typeof item !== "string" || !pattern.test(item)
        || (key === "httpHosts" && /\.(localhost|local|internal)$/.test(item))))
        throw new Error(`Invalid scope: ${key}`);
    } else if (key === "maxMessagesPerHour") {
      if (!Number.isInteger(value) || value < 1 || value > 60) throw new Error("Invalid message limit");
    } else if (typeof value !== "boolean") throw new Error(`Invalid permission: ${key}`);
  }
  for (const [key, max] of [["author", 80], ["description", 2000]]) {
    if (manifest[key] !== undefined && (typeof manifest[key] !== "string"
      || manifest[key].length > max)) throw new Error(`Invalid ${key}`);
  }
  return manifest;
}

export async function pack(folder, output) {
  const root = resolve(folder);
  const manifest = validateManifest(JSON.parse(await readFile(join(root, "manifest.json"), "utf8")));
  const result = await build({
    entryPoints: [join(root, "index.ts")], bundle: true, write: false,
    format: "iife", globalName: "JellyPlugin", platform: "neutral", target: "es2016",
    metafile: true, legalComments: "none",
    plugins: [{ name: "no-native-imports", setup(builder) {
      builder.onResolve({ filter: /^(node:|https?:|file:|data:)/ }, args =>
        ({ errors: [{ text: `Unavailable plugin import: ${args.path}` }] }));
    } }],
  });
  if (Object.values(result.metafile.outputs).some(file => file.imports.length))
    throw new Error("All dependencies must be bundled");
  const code = result.outputFiles[0].text;
  const data = JSON.stringify({ manifest, code }, null, 2);
  if (Buffer.byteLength(data, "utf8") > 1024 * 1024) throw new Error("Package exceeds 1 MiB");
  const path = output ? resolve(output) : join(root, "dist", `${manifest.id}.jelly`);
  await mkdir(resolve(path, ".."), { recursive: true });
  await writeFile(path, data + "\n", "utf8");
  return path;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  if (!process.argv[2]) throw new Error("Usage: node scripts/pack.mjs PLUGIN_FOLDER [OUTPUT]");
  console.log(await pack(process.argv[2], process.argv[3]));
}
