import contract from "./contract.json" with { type: "json" };

export { contract };

const stringify = JSON.stringify;
const parse = JSON.parse;
const keys = Object.keys;
const has = Function.prototype.call.bind(Object.prototype.hasOwnProperty);
const matches = (pattern, text) => typeof text === "string" && pattern.exec(text)?.[0] === text;
const encoderLength = text => {
  let length = 0;
  for (const char of text) {
    const code = char.codePointAt(0);
    length += code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
  }
  return length;
};

export class ApiError extends Error {
  constructor(code, message = code, stage = "validation", retryAfter = 0) {
    super(message);
    Object.defineProperty(this, "name", { value: "JellyError", configurable: true });
    this.code = code;
    this.stage = stage;
    this.retryAfter = retryAfter;
  }
}

export function object(value, allowed, required = allowed) {
  if (!value || typeof value !== "object" || Array.isArray(value)
    || keys(value).some(key => !allowed.includes(key))
    || required.some(key => !has(value, key))) {
    throw new ApiError("INVALID_OBJECT");
  }
  return value;
}

export function jsonCopy(value, limit = contract.limits.jsonBytes) {
  const ancestors = new Set();
  let nodes = 0;
  const copy = (item, depth) => {
    if (++nodes > contract.limits.jsonNodes || depth > contract.limits.jsonDepth) throw new ApiError("JSON_LIMIT");
    if (item === null || typeof item === "string" || typeof item === "boolean") return item;
    if (typeof item === "number" && Number.isFinite(item)) return item;
    if (!item || typeof item !== "object" || ancestors.has(item)) throw new ApiError("INVALID_JSON_VALUE");
    ancestors.add(item);
    const array = Array.isArray(item);
    const prototype = Object.getPrototypeOf(item);
    if (!array && prototype !== Object.prototype && prototype !== null) throw new ApiError("INVALID_JSON_VALUE");
    const names = array ? null : keys(item);
    if ((array ? item.length : names.length) > contract.limits.jsonNodes) throw new ApiError("JSON_LIMIT");
    const result = array ? [] : Object.create(null);
    for (const key of array ? Array.from({ length: item.length }, (_, index) => index) : names) {
      result[key] = copy(item[key], depth + 1);
    }
    ancestors.delete(item);
    return result;
  };
  const text = stringify(copy(value, 0));
  if (typeof text !== "string" || encoderLength(text) > limit) {
    throw new ApiError("JSON_LIMIT");
  }
  return parse(text);
}

export function validateParams(method, input) {
  if (!has(contract.methods, method)) throw new ApiError("METHOD_UNAVAILABLE");
  const schema = contract.methods[method].params;
  object(input, keys(schema), []);
  const result = {};
  for (const [key, field] of Object.entries(schema)) {
    const value = has(input, key) ? input[key] : field.default;
    const valid = field.type === "chat" ? matches(/^-?[1-9][0-9]{0,15}$/, value)
      : field.type === "key" ? matches(/^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}$/, value)
      : field.type === "string" ? typeof value === "string" && value.length >= field.min && value.length <= field.max
      : field.type === "integer" ? Number.isInteger(value) && value >= field.min && value <= field.max
      : field.type === "boolean" ? typeof value === "boolean"
      : field.type === "enum" ? field.values.includes(value)
      : field.type === "json" && value !== undefined;
    if (!valid) throw new ApiError("INVALID_PARAMETER", `Invalid ${method}.${key}`);
    result[key] = field.type === "json" ? jsonCopy(value) : value;
  }
  return result;
}

export function validatePackage(input) {
  object(input, ["formatVersion", "manifest", "code"]);
  if (input.formatVersion !== contract.packageVersion) throw new ApiError("PACKAGE_VERSION_UNSUPPORTED");
  const manifest = object(input.manifest,
    ["id", "name", "version", "apiVersion", "author", "description", "capabilities"],
    ["id", "name", "version", "apiVersion", "capabilities"]);
  if (manifest.apiVersion !== contract.apiVersion) throw new ApiError("API_VERSION_UNSUPPORTED");
  if (!matches(/^[a-z][a-z0-9.-]{2,63}$/, manifest.id)
    || !matches(/^[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}$/, manifest.version)) {
    throw new ApiError("INVALID_MANIFEST");
  }
  for (const [key, maximum] of [["name", 80], ["author", 80], ["description", 2000]]) {
    if ((key === "name" || has(manifest, key)) && (typeof manifest[key] !== "string"
      || manifest[key].length > maximum || (key === "name" && !manifest[key].trim()))) {
      throw new ApiError("INVALID_MANIFEST", `Invalid ${key}`);
    }
  }
  const categories = new Set(Object.values(contract.methods).map(method => method.category));
  if (!Array.isArray(manifest.capabilities) || manifest.capabilities.length > categories.size
    || new Set(manifest.capabilities).size !== manifest.capabilities.length
    || manifest.capabilities.some(category => !categories.has(category))) {
    throw new ApiError("INVALID_CAPABILITIES");
  }
  if (typeof input.code !== "string" || !input.code.trim() || input.code.includes("\0")) {
    throw new ApiError("INVALID_CODE");
  }
  return jsonCopy(input, contract.limits.packageBytes);
}
