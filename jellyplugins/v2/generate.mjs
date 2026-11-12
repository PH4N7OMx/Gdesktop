import { build } from "esbuild";
import { readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
import { contract } from "./contract.mjs";

const typeOf = field => field.type === "integer" ? "number" : field.type === "boolean" ? "boolean"
  : field.type === "json" ? "JsonValue" : field.type === "enum" ? field.values.map(value => JSON.stringify(value)).join(" | ") : "string";
const methods = Object.entries(contract.methods).map(([name, method]) =>
  `  ${JSON.stringify(name)}: { params: { ${Object.entries(method.params).map(([key, field]) =>
    `${key}${Object.hasOwn(field, "default") ? "?" : ""}: ${typeOf(field)}`).join("; ")} }; result: ${method.result} };`).join("\n");
const types = `export type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };\nexport interface Methods {\n${methods}\n}\nexport interface JellyApi {\n  readonly telegram: {\n    sendText(params: Methods["telegram.sendText"]["params"]): Promise<Methods["telegram.sendText"]["result"]>;\n    readHistory(params: Methods["telegram.readHistory"]["params"]): Promise<Methods["telegram.readHistory"]["result"]>;\n  };\n  readonly schedule: {\n    every(id: string, seconds: number, handler: () => void | Promise<void>, options?: { first?: "after-period" | "immediate" }): Promise<void>;\n    cancel(id: string): Promise<void>;\n  };\n  readonly storage: { get(key: string): Promise<JsonValue>; set(key: string, value: JsonValue): Promise<boolean> };\n  readonly log: { info(message: string): void; error(message: string): void };\n  readonly settings: Readonly<{ [key: string]: JsonValue }>;\n}\nexport type Plugin = (api: JellyApi) => void | (() => void | Promise<void>) | Promise<void | (() => void | Promise<void>)>;\n`;
const bundle = await build({ entryPoints: [fileURLToPath(new URL("runtime.mjs", import.meta.url))],
  absWorkingDir: fileURLToPath(new URL(".", import.meta.url)),
  bundle: true, write: false, format: "iife", globalName: "runtimeExports", platform: "neutral", target: "es2022", legalComments: "none" });
const bootstrap = `(() => {\n${bundle.outputFiles[0].text}\nreturn runtimeExports.createRuntime;\n})()\n`;
if (bootstrap.includes(')JELLY_BOOTSTRAP"') || Buffer.byteLength(bootstrap) >= 65535) {
  throw new Error("Bootstrap exceeds the native literal format; update its embedding");
}
const characters = Array.from(bootstrap);
const literals = [];
for (let offset = 0; offset < characters.length; offset += 2048) {
  literals.push(`R\"JELLY_BOOTSTRAP(${characters.slice(offset, offset + 2048).join("")})JELLY_BOOTSTRAP\"`);
}
const constants = {
  kProtocolVersion: contract.protocolVersion, kApiVersion: contract.apiVersion, kPackageVersion: contract.packageVersion,
  kFrameBytes: `std::size_t(${contract.limits.frameBytes})`, kMemoryBytes: `std::size_t(${contract.limits.memoryBytes})`,
  kStackBytes: `std::size_t(${contract.limits.stackBytes})`, kTurnMilliseconds: contract.limits.turnMilliseconds,
  kJobsPerTurn: contract.limits.jobsPerTurn, kFramesPerTurn: contract.limits.framesPerTurn,
};
const header = ["#pragma once", "", "#include <cstddef>", "", "namespace JellyPlugins::Host {", "",
  ...Object.entries(constants).map(([name, value]) => `inline constexpr auto ${name} = ${value};`),
  "inline constexpr auto kBootstrap =", ...literals, ";",
  "", "} // namespace JellyPlugins::Host", ""].join("\n");
const catalog = JSON.stringify(contract);
const revision = createHash("sha256").update(catalog).digest("hex");
const clientHeader = ["#pragma once", "", "namespace JellyPlugins::Client {", "",
  `inline constexpr auto kContractJson = R\"JELLY_CONTRACT(${catalog})JELLY_CONTRACT\";`,
  `inline constexpr auto kContractRevision = \"${revision}\";`, "", "} // namespace JellyPlugins::Client", ""].join("\n");
for (const [name, content] of [["sdk.d.ts", types], ["host/bootstrap.generated.h", header], ["client/contract.generated.h", clientHeader]]) {
  const path = new URL(name, import.meta.url);
  const normalized = content.replace(/\r?\n/g, "\r\n");
  if (process.argv.includes("--check")) {
    if ((await readFile(path, "utf8")).replace(/\r\n/g, "\n") !== content) throw new Error(`Regenerate ${name}: npm run generate:v2`);
  } else {
    await writeFile(path, normalized, "utf8");
  }
}
