import { ApiError, contract, jsonCopy, object, validatePackage, validateParams } from "./contract.mjs";

export function createRuntime(nativeSend) {
  const locked = new Set();
  const lock = value => {
    if (!value || (typeof value !== "object" && typeof value !== "function") || locked.has(value)) return;
    locked.add(value);
    lock(Object.getPrototypeOf(value));
    for (const key of Reflect.ownKeys(value)) {
      const property = Object.getOwnPropertyDescriptor(value, key);
      if (Object.hasOwn(property, "value")) lock(property.value);
      else { lock(property.get); lock(property.set); }
    }
    Object.freeze(value);
  };
  for (const name of ["Object", "Array", "Function", "Map", "Set", "WeakMap", "WeakSet", "Promise", "JSON",
    "Reflect", "Math", "Error", "TypeError", "RangeError", "SyntaxError", "RegExp", "String", "Number", "Boolean",
    "Symbol", "BigInt", "Date", "Proxy", "ArrayBuffer", "DataView", "Uint8Array", "Int8Array", "Uint16Array",
    "Int16Array", "Uint32Array", "Int32Array", "Float32Array", "Float64Array", "BigInt64Array", "BigUint64Array"]) {
    const value = globalThis[name];
    if (!value) continue;
    lock(value);
    Object.defineProperty(globalThis, name, { value, writable: false, configurable: false });
  }
  lock(contract);
  lock(ApiError);
  const stringify = JSON.stringify;
  const parse = JSON.parse;
  const freeze = Object.freeze;
  const resolve = Promise.resolve.bind(Promise);
  const pending = new Map();
  const schedules = new Map();
  const busy = new Set();
  let identity;
  let configuration;
  let phase = "idle";
  let failure;
  let nextRequest = 0;
  let lastEvent = 0;
  let dispose;

  const send = frame => nativeSend(stringify({ protocolVersion: contract.protocolVersion, ...identity, ...frame }));
  const errorData = error => {
    let code = "PLUGIN_ERROR";
    let message = "Plugin execution failed";
    try {
      if (error instanceof ApiError && typeof error.code === "string") code = error.code.slice(0, 80);
      if (typeof error === "string") message = error.slice(0, 500);
      else if (typeof error?.message === "string") message = error.message.slice(0, 500);
    } catch {}
    return { code, message, stage: "execution" };
  };
  const fail = error => {
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
    if (pending.size >= contract.limits.pendingRequests || nextRequest >= 2147483647) {
      return Promise.reject(new ApiError("REQUEST_LIMIT"));
    }
    let params;
    try { params = validateParams(method, input); } catch (error) { return Promise.reject(error); }
    const requestId = ++nextRequest;
    return new Promise((accept, reject) => {
      pending.set(requestId, { accept, reject });
      try { send({ type: "request", requestId, method, params }); }
      catch (error) { pending.delete(requestId); reject(error); }
    });
  };
  const log = (level, message) => {
    if (phase !== "activating" && phase !== "ready") return;
    send({ type: "log", level, message: String(message).slice(0, contract.limits.logCharacters) });
  };
  const api = freeze({
    telegram: freeze({
      sendText: params => call("telegram.sendText", params),
      readHistory: params => call("telegram.readHistory", params),
    }),
    schedule: freeze({
      async every(id, seconds, handler, options = {}) {
        if (typeof handler !== "function" || schedules.has(id) || schedules.size >= contract.limits.schedules) {
          throw new ApiError("INVALID_SCHEDULE");
        }
        object(options, ["first"], []);
        const params = validateParams("schedule.every", { id, seconds, ...options });
        schedules.set(id, handler);
        try { await call("schedule.every", params); }
        catch (error) { schedules.delete(id); throw error; }
      },
      async cancel(id) {
        await call("schedule.cancel", { id });
        schedules.delete(id);
      },
    }),
    storage: freeze({ get: key => call("storage.get", { key }), set: (key, value) => call("storage.set", { key, value }) }),
    log: freeze({ info: message => log("info", message), error: message => log("error", message) }),
    get settings() { return freeze(jsonCopy(configuration)); },
  });

  return freeze({
    initialize(text) {
      if (phase !== "idle") throw new ApiError("ALREADY_INITIALIZED");
      const frame = object(parse(text), ["type", "protocolVersion", "instanceId", "generation", "package", "settings"]);
      if (frame.type !== "init" || frame.protocolVersion !== contract.protocolVersion
        || typeof frame.instanceId !== "string" || /^[a-zA-Z0-9-]{1,80}$/.exec(frame.instanceId)?.[0] !== frame.instanceId
        || !Number.isSafeInteger(frame.generation) || frame.generation < 1) throw new ApiError("INVALID_INIT");
      const pkg = validatePackage(frame.package);
      object(frame.settings, Object.keys(frame.settings), []);
      configuration = jsonCopy(frame.settings);
      identity = freeze({ instanceId: frame.instanceId, generation: frame.generation });
      phase = "activating";
      return pkg.code;
    },
    activate(activation) {
      if (phase !== "activating" || typeof activation !== "function") throw new ApiError("INVALID_ENTRY_POINT");
      resolve().then(() => activation(api)).then(cleanup => {
        if (cleanup !== undefined && typeof cleanup !== "function") throw new ApiError("INVALID_DISPOSER");
        dispose = cleanup;
        if (phase === "activating") {
          phase = "ready";
          send({ type: "ready" });
        }
      }).catch(error => { if (phase === "activating") fail(error); });
    },
    dispatch(text) {
      const frame = parse(text);
      if (!frame || frame.protocolVersion !== contract.protocolVersion || !identity
        || frame.instanceId !== identity.instanceId || frame.generation !== identity.generation) {
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
        object(frame, ["type", "protocolVersion", "instanceId", "generation", "requestId", "value", "error"],
          ["type", "protocolVersion", "instanceId", "generation", "requestId"]);
        if (!Number.isInteger(frame.requestId) || frame.requestId < 1
          || Object.hasOwn(frame, "value") === Object.hasOwn(frame, "error")) throw new ApiError("INVALID_RESULT");
        const entry = pending.get(frame.requestId);
        if (!entry) return;
        if (Object.hasOwn(frame, "error")) {
          object(frame.error, ["code", "message", "stage", "retryAfter"], ["code", "message", "stage"]);
          if (typeof frame.error.code !== "string" || typeof frame.error.message !== "string"
            || typeof frame.error.stage !== "string" || (frame.error.retryAfter !== undefined
              && (!Number.isInteger(frame.error.retryAfter) || frame.error.retryAfter < 0))) throw new ApiError("INVALID_RESULT");
          pending.delete(frame.requestId);
          entry.reject(new ApiError(frame.error.code, frame.error.message, frame.error.stage, frame.error.retryAfter));
        } else {
          pending.delete(frame.requestId);
          entry.accept(jsonCopy(frame.value, contract.limits.frameBytes));
        }
      } else if (frame.type === "event") {
        object(frame, ["type", "protocolVersion", "instanceId", "generation", "eventId", "event", "scheduleId"]);
        if (phase !== "ready" || frame.event !== "schedule" || !Number.isSafeInteger(frame.eventId)
          || frame.eventId <= lastEvent || typeof frame.scheduleId !== "string") throw new ApiError("INVALID_EVENT");
        lastEvent = frame.eventId;
        const handler = schedules.get(frame.scheduleId);
        if (!handler || busy.has(frame.scheduleId)) {
          send({ type: "eventDone", eventId: frame.eventId, outcome: "skipped" });
          return;
        }
        busy.add(frame.scheduleId);
        resolve().then(handler).then(() => "completed", error => {
          log("error", errorData(error).message);
          return "failed";
        }).then(outcome => {
          busy.delete(frame.scheduleId);
          if (phase === "ready") send({ type: "eventDone", eventId: frame.eventId, outcome });
        }).catch(fail);
      } else {
        throw new ApiError("INVALID_FRAME");
      }
    },
    state: () => phase,
    completeTurn: turnId => send({ type: "turnComplete", turnId }),
  });
}
