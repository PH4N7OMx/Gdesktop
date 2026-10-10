import { build } from "esbuild";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { resolve, join } from "node:path";
import { pathToFileURL } from "node:url";

const allowedPermissions = new Set([
  "readChats", "sendChats", "httpHosts", "joinChannels", "botChats", "attachmentChats", "editChats", "reactionChats", "historyChats", "storage", "timers", "ui", "maxMessagesPerHour", "fileRead", "fileWrite", "moneyRead", "webviewBots", "menuChats", "uiDialogs",
]);

export function validateSettingsSchema(schema) {
  if (schema === undefined) return;
  if (!Array.isArray(schema) || schema.length > 16 || Buffer.byteLength(JSON.stringify(schema), "utf8") > 4096)
    throw new Error("Invalid settings schema size");
  const keys = new Set();
  const allowed = new Set(["key", "label", "type", "default", "min", "max", "maxLength", "options"]);
  for (const field of schema) {
    if (!field || typeof field !== "object" || Array.isArray(field)
      || Object.keys(field).some(k => !allowed.has(k))
      || typeof field.key !== "string" || /^[a-z][a-zA-Z0-9_.-]{0,63}$/.exec(field.key)?.[0] !== field.key
      || keys.has(field.key) || typeof field.label !== "string" || !field.label || field.label.length > 80)
      throw new Error("Invalid settings field");
    keys.add(field.key);
    if (field.maxLength !== undefined && (field.type !== "string" || !Number.isInteger(field.maxLength)
      || field.maxLength < 1 || field.maxLength > 2000)) throw new Error("Invalid string limit");
    for (const bound of ["min", "max"]) if (field[bound] !== undefined && (field.type !== "number"
      || !Number.isFinite(field[bound]) || Math.abs(field[bound]) > 1e9)) throw new Error("Invalid number range");
    if ((field.min ?? -1e9) > (field.max ?? 1e9)) throw new Error("Invalid number range");
    if (field.type === "select") {
      if (!Array.isArray(field.options) || !field.options.length || field.options.length > 8
        || new Set(field.options).size !== field.options.length
        || field.options.some(v => typeof v !== "string" || !v || v.length > 80)) throw new Error("Invalid options");
    } else if (field.options !== undefined) throw new Error("Unexpected options");
    const valid = field.type === "boolean" ? typeof field.default === "boolean"
      : field.type === "string" ? typeof field.default === "string" && field.default.length <= (field.maxLength ?? 512)
      : field.type === "number" ? Number.isFinite(field.default) && field.default >= (field.min ?? -1e9) && field.default <= (field.max ?? 1e9)
      : field.type === "select" && typeof field.default === "string" && field.options.includes(field.default);
    if (!valid) throw new Error("Invalid setting default");
  }
}

export function validateManifest(manifest) {
  if (!manifest || manifest.apiVersion !== 1 || !/^[a-z][a-z0-9.-]{2,63}$/.test(manifest.id)
    || typeof manifest.name !== "string" || !manifest.name || manifest.name.length > 80
    || !/^\d+\.\d+\.\d+$/.test(manifest.version)
    || !manifest.permissions || Array.isArray(manifest.permissions)
    || typeof manifest.permissions !== "object") throw new Error("Invalid manifest");
  for (const [key, value] of Object.entries(manifest.permissions)) {
    if (!allowedPermissions.has(key)) throw new Error(`Unknown permission: ${key}`);
    if (["readChats", "sendChats", "httpHosts", "joinChannels", "botChats", "attachmentChats", "editChats", "reactionChats", "historyChats", "webviewBots", "menuChats"].includes(key)) {
      if (!Array.isArray(value) || value.length > 32) throw new Error(`Invalid scope: ${key}`);
      const pattern = key === "httpHosts"
        ? /^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z]{2,63}$/
        : key === "webviewBots" ? /^[1-9][0-9]{0,15}$/ : /^-?[1-9][0-9]{0,15}$/;
      if (value.some(item => typeof item !== "string" || pattern.exec(item)?.[0] !== item
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
  validateSettingsSchema(manifest.settingsSchema);
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
