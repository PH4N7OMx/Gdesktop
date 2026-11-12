#pragma once

#include <cstddef>

namespace JellyPlugins::Host {

inline constexpr auto kProtocolVersion = 2;
inline constexpr auto kApiVersion = 2;
inline constexpr auto kPackageVersion = 2;
inline constexpr auto kFrameBytes = std::size_t(2097152);
inline constexpr auto kMemoryBytes = std::size_t(67108864);
inline constexpr auto kStackBytes = std::size_t(524288);
inline constexpr auto kTurnMilliseconds = 250;
inline constexpr auto kJobsPerTurn = 10000;
inline constexpr auto kFramesPerTurn = 100;
inline constexpr auto kBootstrap =
R"JELLY_BOOTSTRAP((() => {
"use strict";
var runtimeExports = (() => {
  var __defProp = Object.defineProperty;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __export = (target, all) => {
    for (var name in all)
      __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __copyProps = (to, from, except, desc) => {
    if (from && typeof from === "object" || typeof from === "function") {
      for (let key of __getOwnPropNames(from))
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
    }
    return to;
  };
  var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

  // runtime.mjs
  var runtime_exports = {};
  __export(runtime_exports, {
    createRuntime: () => createRuntime
  });

  // contract.json
  var contract_default = {
    packageVersion: 2,
    apiVersion: 2,
    protocolVersion: 2,
    limits: {
      packageBytes: 1048576,
      frameBytes: 2097152,
      memoryBytes: 67108864,
      stackBytes: 524288,
      turnMilliseconds: 250,
      jobsPerTurn: 1e4,
      pendingRequests: 32,
      schedules: 16,
      jsonBytes: 65536,
      jsonDepth: 32,
      jsonNodes: 16384,
      framesPerTurn: 100,
      logCharacters: 2e3,
      startupMilliseconds: 1e4,
      activationMilliseconds: 3e4,
      transportMilliseconds: 3e3,
      stopMilliseconds: 1e3,
      requestMilliseconds: 3e4,
      approvalMilliseconds: 3e5,
      installedPlugins: 16,
      savedRules: 64,
      pluginOperationsPerHour: 20,
      accountOperationsPerHour: 60
    },
    methods: {
      "telegram.sendText": {
        category: "telegram.send",
        access: "protected",
        params: {
          chatId: { type: "chat" },
          text: { type: "string", min: 1, max: 4096 },
          silent: { type: "boolean", default: false }
  )JELLY_BOOTSTRAP"
R"JELLY_BOOTSTRAP(      },
        result: "{ messageId: string }"
      },
      "telegram.readHistory": {
        category: "telegram.read",
        access: "protected",
        params: {
          chatId: { type: "chat" },
          limit: { type: "integer", min: 1, max: 100, default: 20 }
        },
        result: "ReadonlyArray<{ id: string; text: string }>"
      },
      "schedule.every": {
        category: "schedule",
        access: "local",
        params: {
          id: { type: "key" },
          seconds: { type: "integer", min: 60, max: 604800 },
          first: { type: "enum", values: ["after-period", "immediate"], default: "after-period" }
        },
        result: "boolean"
      },
      "schedule.cancel": {
        category: "schedule",
        access: "local",
        params: { id: { type: "key" } },
        result: "boolean"
      },
      "storage.get": {
        category: "storage",
        access: "local",
        params: { key: { type: "key" } },
        result: "JsonValue"
      },
      "storage.set": {
        category: "storage",
        access: "local",
        params: { key: { type: "key" }, value: { type: "json" } },
        result: "boolean"
      }
    }
  };

  // contract.mjs
  var stringify = JSON.stringify;
  var parse = JSON.parse;
  var keys = Object.keys;
  var has = Function.prototype.call.bind(Object.prototype.hasOwnProperty);
  var matches = (pattern, text) => typeof text === "string" && pattern.exec(text)?.[0] === text;
  var encoderLength = (text) => {
    let length = 0;
    for (const char of text) {
      const code = char.codePointAt(0);
      length += code < 128 ? 1 : code < 2048 ? 2 : code < 65536 ? 3 : 4;
    }
    return length;
  };
  var ApiError = class extends Error {
    constructor(code, message = code, stage = "validation", retryAfter = 0) {
      super(message);
      Object.defineProperty(this, "name", { value: "JellyError", configurable: true });
      this.code = code;
      this.stage = stage;
      this.retryAfter = retryAfter;
    }
  };
  function object(valu)JELLY_BOOTSTRAP"
R"JELLY_BOOTSTRAP(e, allowed, required = allowed) {
    if (!value || typeof value !== "object" || Array.isArray(value) || keys(value).some((key) => !allowed.includes(key)) || required.some((key) => !has(value, key))) {
      throw new ApiError("INVALID_OBJECT");
    }
    return value;
  }
  function jsonCopy(value, limit = contract_default.limits.jsonBytes) {
    const ancestors = /* @__PURE__ */ new Set();
    let nodes = 0;
    const copy = (item, depth) => {
      if (++nodes > contract_default.limits.jsonNodes || depth > contract_default.limits.jsonDepth) throw new ApiError("JSON_LIMIT");
      if (item === null || typeof item === "string" || typeof item === "boolean") return item;
      if (typeof item === "number" && Number.isFinite(item)) return item;
      if (!item || typeof item !== "object" || ancestors.has(item)) throw new ApiError("INVALID_JSON_VALUE");
      ancestors.add(item);
      const array = Array.isArray(item);
      const prototype = Object.getPrototypeOf(item);
      if (!array && prototype !== Object.prototype && prototype !== null) throw new ApiError("INVALID_JSON_VALUE");
      const names = array ? null : keys(item);
      if ((array ? item.length : names.length) > contract_default.limits.jsonNodes) throw new ApiError("JSON_LIMIT");
      const result = array ? [] : /* @__PURE__ */ Object.create(null);
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
  function validateParams(method, input) {
    if (!has(contract_default.methods, method)) throw new ApiError("METHOD_UNAVAILABLE");
    const schema = contract_default.methods[method].params;
    object(input, keys(schema), []);
    const result = {};
    for (const [key, field] of Object.entries(schema)) {
 )JELLY_BOOTSTRAP"
R"JELLY_BOOTSTRAP(     const value = has(input, key) ? input[key] : field.default;
      const valid = field.type === "chat" ? matches(/^-?[1-9][0-9]{0,15}$/, value) : field.type === "key" ? matches(/^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}$/, value) : field.type === "string" ? typeof value === "string" && value.length >= field.min && value.length <= field.max : field.type === "integer" ? Number.isInteger(value) && value >= field.min && value <= field.max : field.type === "boolean" ? typeof value === "boolean" : field.type === "enum" ? field.values.includes(value) : field.type === "json" && value !== void 0;
      if (!valid) throw new ApiError("INVALID_PARAMETER", `Invalid ${method}.${key}`);
      result[key] = field.type === "json" ? jsonCopy(value) : value;
    }
    return result;
  }
  function validatePackage(input) {
    object(input, ["formatVersion", "manifest", "code"]);
    if (input.formatVersion !== contract_default.packageVersion) throw new ApiError("PACKAGE_VERSION_UNSUPPORTED");
    const manifest = object(
      input.manifest,
      ["id", "name", "version", "apiVersion", "author", "description", "capabilities"],
      ["id", "name", "version", "apiVersion", "capabilities"]
    );
    if (manifest.apiVersion !== contract_default.apiVersion) throw new ApiError("API_VERSION_UNSUPPORTED");
    if (!matches(/^[a-z][a-z0-9.-]{2,63}$/, manifest.id) || !matches(/^[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}$/, manifest.version)) {
      throw new ApiError("INVALID_MANIFEST");
    }
    for (const [key, maximum] of [["name", 80], ["author", 80], ["description", 2e3]]) {
      if ((key === "name" || has(manifest, key)) && (typeof manifest[key] !== "string" || manifest[key].length > maximum || key === "name" && !manifest[key].trim())) {
        throw new ApiError("INVALID_MANIFEST", `Invalid ${key}`);
      }
    }
    const categories = new Set(Object.values(contract_default.methods).map((method) => method.category));
    if (!Array.isArray(manifest.capabilities) || manifest.capabilities.length > categories.size || new Set(manifest.capabi)JELLY_BOOTSTRAP"
R"JELLY_BOOTSTRAP(lities).size !== manifest.capabilities.length || manifest.capabilities.some((category) => !categories.has(category))) {
      throw new ApiError("INVALID_CAPABILITIES");
    }
    if (typeof input.code !== "string" || !input.code.trim() || input.code.includes("\0")) {
      throw new ApiError("INVALID_CODE");
    }
    return jsonCopy(input, contract_default.limits.packageBytes);
  }

  // runtime.mjs
  function createRuntime(nativeSend) {
    const locked = /* @__PURE__ */ new Set();
    const lock = (value) => {
      if (!value || typeof value !== "object" && typeof value !== "function" || locked.has(value)) return;
      locked.add(value);
      lock(Object.getPrototypeOf(value));
      for (const key of Reflect.ownKeys(value)) {
        const property = Object.getOwnPropertyDescriptor(value, key);
        if (Object.hasOwn(property, "value")) lock(property.value);
        else {
          lock(property.get);
          lock(property.set);
        }
      }
      Object.freeze(value);
    };
    for (const name of [
      "Object",
      "Array",
      "Function",
      "Map",
      "Set",
      "WeakMap",
      "WeakSet",
      "Promise",
      "JSON",
      "Reflect",
      "Math",
      "Error",
      "TypeError",
      "RangeError",
      "SyntaxError",
      "RegExp",
      "String",
      "Number",
      "Boolean",
      "Symbol",
      "BigInt",
      "Date",
      "Proxy",
      "ArrayBuffer",
      "DataView",
      "Uint8Array",
      "Int8Array",
      "Uint16Array",
      "Int16Array",
      "Uint32Array",
      "Int32Array",
      "Float32Array",
      "Float64Array",
      "BigInt64Array",
      "BigUint64Array"
    ]) {
      const value = globalThis[name];
      if (!value) continue;
      lock(value);
      Object.defineProperty(globalThis, name, { value, writable: false, configurable: false });
    }
    lock(contract_default);
    lock(ApiError);
    const stringify2 = JSON.stringify;
    const parse2 = JSON.parse;
    const freeze = Object.freeze;
    const resolve = Promise.resolve.bind(Pr)JELLY_BOOTSTRAP"
R"JELLY_BOOTSTRAP(omise);
    const pending = /* @__PURE__ */ new Map();
    const schedules = /* @__PURE__ */ new Map();
    const busy = /* @__PURE__ */ new Set();
    let identity;
    let configuration;
    let phase = "idle";
    let failure;
    let nextRequest = 0;
    let lastEvent = 0;
    let dispose;
    const send = (frame) => nativeSend(stringify2({ protocolVersion: contract_default.protocolVersion, ...identity, ...frame }));
    const errorData = (error) => {
      let code = "PLUGIN_ERROR";
      let message = "Plugin execution failed";
      try {
        if (error instanceof ApiError && typeof error.code === "string") code = error.code.slice(0, 80);
        if (typeof error === "string") message = error.slice(0, 500);
        else if (typeof error?.message === "string") message = error.message.slice(0, 500);
      } catch {
      }
      return { code, message, stage: "execution" };
    };
    const fail = (error) => {
      if (phase === "failed" || phase === "stopped") return;
      failure = errorData(error);
      phase = "failed";
      for (const entry of pending.values()) entry.reject(new ApiError("INSTANCE_STOPPED"));
      pending.clear();
      schedules.clear();
      send({ type: "fatal", error: failure });
    };
    const call = (method, input) => {
      if (phase !== "activating" && phase !== "ready") return Promise.reject(new ApiError("INSTANCE_STOPPED"));
      if (pending.size >= contract_default.limits.pendingRequests || nextRequest >= 2147483647) {
        return Promise.reject(new ApiError("REQUEST_LIMIT"));
      }
      let params;
      try {
        params = validateParams(method, input);
      } catch (error) {
        return Promise.reject(error);
      }
      const requestId = ++nextRequest;
      return new Promise((accept, reject) => {
        pending.set(requestId, { accept, reject });
        try {
          send({ type: "request", requestId, method, params });
        } catch (error) {
          pending.delete(requestId);
          reject(error);
        }
      });
    };
    co)JELLY_BOOTSTRAP"
R"JELLY_BOOTSTRAP(nst log = (level, message) => {
      if (phase !== "activating" && phase !== "ready") return;
      send({ type: "log", level, message: String(message).slice(0, contract_default.limits.logCharacters) });
    };
    const api = freeze({
      telegram: freeze({
        sendText: (params) => call("telegram.sendText", params),
        readHistory: (params) => call("telegram.readHistory", params)
      }),
      schedule: freeze({
        async every(id, seconds, handler, options = {}) {
          if (typeof handler !== "function" || schedules.has(id) || schedules.size >= contract_default.limits.schedules) {
            throw new ApiError("INVALID_SCHEDULE");
          }
          object(options, ["first"], []);
          const params = validateParams("schedule.every", { id, seconds, ...options });
          schedules.set(id, handler);
          try {
            await call("schedule.every", params);
          } catch (error) {
            schedules.delete(id);
            throw error;
          }
        },
        async cancel(id) {
          await call("schedule.cancel", { id });
          schedules.delete(id);
        }
      }),
      storage: freeze({ get: (key) => call("storage.get", { key }), set: (key, value) => call("storage.set", { key, value }) }),
      log: freeze({ info: (message) => log("info", message), error: (message) => log("error", message) }),
      get settings() {
        return freeze(jsonCopy(configuration));
      }
    });
    return freeze({
      initialize(text) {
        if (phase !== "idle") throw new ApiError("ALREADY_INITIALIZED");
        const frame = object(parse2(text), ["type", "protocolVersion", "instanceId", "generation", "package", "settings"]);
        if (frame.type !== "init" || frame.protocolVersion !== contract_default.protocolVersion || typeof frame.instanceId !== "string" || /^[a-zA-Z0-9-]{1,80}$/.exec(frame.instanceId)?.[0] !== frame.instanceId || !Number.isSafeInteger(frame.generation) || frame.generation < 1) throw new ApiError("INVALID_INIT");
        const pkg =)JELLY_BOOTSTRAP"
R"JELLY_BOOTSTRAP( validatePackage(frame.package);
        object(frame.settings, Object.keys(frame.settings), []);
        configuration = jsonCopy(frame.settings);
        identity = freeze({ instanceId: frame.instanceId, generation: frame.generation });
        phase = "activating";
        return pkg.code;
      },
      activate(activation) {
        if (phase !== "activating" || typeof activation !== "function") throw new ApiError("INVALID_ENTRY_POINT");
        resolve().then(() => activation(api)).then((cleanup) => {
          if (cleanup !== void 0 && typeof cleanup !== "function") throw new ApiError("INVALID_DISPOSER");
          dispose = cleanup;
          if (phase === "activating") {
            phase = "ready";
            send({ type: "ready" });
          }
        }).catch((error) => {
          if (phase === "activating") fail(error);
        });
      },
      dispatch(text) {
        const frame = parse2(text);
        if (!frame || frame.protocolVersion !== contract_default.protocolVersion || !identity || frame.instanceId !== identity.instanceId || frame.generation !== identity.generation) {
          throw new ApiError("STALE_INSTANCE");
        }
        if (frame.type === "stop") {
          object(frame, ["type", "protocolVersion", "instanceId", "generation"]);
          if (phase === "stopping" || phase === "stopped") return;
          phase = "stopping";
          schedules.clear();
          for (const entry of pending.values()) entry.reject(new ApiError("INSTANCE_STOPPED"));
          pending.clear();
          resolve().then(() => dispose?.()).then(() => {
            phase = "stopped";
            send({ type: "stopped" });
          }).catch(fail);
        } else if (frame.type === "result") {
          object(
            frame,
            ["type", "protocolVersion", "instanceId", "generation", "requestId", "value", "error"],
            ["type", "protocolVersion", "instanceId", "generation", "requestId"]
          );
          if (!Number.isInteger(frame.requestId) || frame.requestId < 1 || Obje)JELLY_BOOTSTRAP"
R"JELLY_BOOTSTRAP(ct.hasOwn(frame, "value") === Object.hasOwn(frame, "error")) throw new ApiError("INVALID_RESULT");
          const entry = pending.get(frame.requestId);
          if (!entry) return;
          if (Object.hasOwn(frame, "error")) {
            object(frame.error, ["code", "message", "stage", "retryAfter"], ["code", "message", "stage"]);
            if (typeof frame.error.code !== "string" || typeof frame.error.message !== "string" || typeof frame.error.stage !== "string" || frame.error.retryAfter !== void 0 && (!Number.isInteger(frame.error.retryAfter) || frame.error.retryAfter < 0)) throw new ApiError("INVALID_RESULT");
            pending.delete(frame.requestId);
            entry.reject(new ApiError(frame.error.code, frame.error.message, frame.error.stage, frame.error.retryAfter));
          } else {
            pending.delete(frame.requestId);
            entry.accept(jsonCopy(frame.value, contract_default.limits.frameBytes));
          }
        } else if (frame.type === "event") {
          object(frame, ["type", "protocolVersion", "instanceId", "generation", "eventId", "event", "scheduleId"]);
          if (phase !== "ready" || frame.event !== "schedule" || !Number.isSafeInteger(frame.eventId) || frame.eventId <= lastEvent || typeof frame.scheduleId !== "string") throw new ApiError("INVALID_EVENT");
          lastEvent = frame.eventId;
          const handler = schedules.get(frame.scheduleId);
          if (!handler || busy.has(frame.scheduleId)) {
            send({ type: "eventDone", eventId: frame.eventId, outcome: "skipped" });
            return;
          }
          busy.add(frame.scheduleId);
          resolve().then(handler).then(() => "completed", (error) => {
            log("error", errorData(error).message);
            return "failed";
          }).then((outcome) => {
            busy.delete(frame.scheduleId);
            if (phase === "ready") send({ type: "eventDone", eventId: frame.eventId, outcome });
          }).catch(fail);
        } else {
          throw new ApiError("INVALID_FRAME");
)JELLY_BOOTSTRAP"
R"JELLY_BOOTSTRAP(        }
      },
      state: () => phase,
      completeTurn: (turnId) => send({ type: "turnComplete", turnId })
    });
  }
  return __toCommonJS(runtime_exports);
})();

return runtimeExports.createRuntime;
})()
)JELLY_BOOTSTRAP"
;

} // namespace JellyPlugins::Host
