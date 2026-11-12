import { build } from "esbuild";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { contract, validatePackage } from "./contract.mjs";

export async function pack(folder, output) {
  const root = resolve(folder);
  const manifest = JSON.parse(await readFile(join(root, "manifest.json"), "utf8"));
  validatePackage({ formatVersion: contract.packageVersion, manifest, code: "export default () => {};" });
  const result = await build({
    entryPoints: [join(root, "index.ts")], absWorkingDir: root, bundle: true, write: false, metafile: true,
    format: "esm", platform: "neutral", target: "es2020", legalComments: "none",
    logOverride: { "unsupported-dynamic-import": "error" }, logLevel: "silent",
    plugins: [{ name: "no-runtime-imports", setup(builder) {
      builder.onResolve({ filter: /^(node:|https?:|file:|data:)/ }, args =>
        ({ errors: [{ text: `Unavailable plugin import: ${args.path}` }] }));
    } }],
  });
  if (Object.values(result.metafile.outputs).some(file => file.imports.length)) {
    throw new Error("All dependencies must be bundled; dynamic imports are unavailable");
  }
  if (!Object.values(result.metafile.outputs).some(file => file.exports.includes("default"))) {
    throw new Error("The plugin must export its activation function as default");
  }
  const data = JSON.stringify(validatePackage({ formatVersion: contract.packageVersion, manifest,
    code: result.outputFiles[0].text }), null, 2) + "\n";
  if (Buffer.byteLength(data) > contract.limits.packageBytes) throw new Error("Package exceeds 1 MiB");
  const path = output ? resolve(output) : join(root, "dist", `${manifest.id}.jelly`);
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, data, "utf8");
  return path;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  if (!process.argv[2]) throw new Error("Usage: node v2/pack.mjs PLUGIN_FOLDER [OUTPUT]");
  console.log(await pack(process.argv[2], process.argv[3]));
}
