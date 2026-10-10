(function (global) {
  "use strict";
  const native = global.__jellyNative;
  delete global.__jellyNative;
  const handlers = new Map();
  const pending = new Map();
  let nextId = 0;
  const post = frame => native.post(JSON.stringify(frame));
  const apiError = (code, retryAfter = 0) => {
    const error = new Error(code);
    error.code = code;
    error.retryAfter = retryAfter;
    return error;
  };
  const request = (method, params = {}) => {
    if (pending.size >= 32) return Promise.reject(apiError("TOO_MANY_REQUESTS"));
    const id = ++nextId;
    return new Promise((resolve, reject) => {
      pending.set(id, { resolve, reject });
      try { post({ type: "request", id, method, params }); }
      catch (error) { pending.delete(id); reject(error); }
    });
  };
  const report = error => post({ type: "log", text: String(error).slice(0, 500) });
  const subscribe = (event, handler) => {
    if (!["message.new", "timer", "action"].includes(event) || typeof handler !== "function")
      throw new Error("INVALID_EVENT");
    const list = handlers.get(event) || [];
    if (list.length >= 32) throw new Error("TOO_MANY_HANDLERS");
    list.push(handler);
    handlers.set(event, list);
    return () => { const index = list.indexOf(handler); if (index >= 0) list.splice(index, 1); };
  };
  const api = Object.freeze({
    apiVersion: 1,
    on: subscribe,
    log: text => post({ type: "log", text: String(text).slice(0, 500) }),
    telegram: Object.freeze({
      joinChannel: chatId => request("telegram.joinChannel", { chatId }),
      clickButton: (chatId, messageId, row, column) => request("telegram.clickButton", { chatId, messageId, row, column }),
      getHistory: (chatId, options = {}) => request("telegram.getHistory", Object.assign({}, options, { chatId })),
      sendAttachment: (chatId, sourceChatId, messageId, options = {}) => request("telegram.sendAttachment", Object.assign({}, options, { chatId, sourceChatId, messageId })),
      editMessage: (chatId, messageId, text) => request("telegram.editMessage", { chatId, messageId, text }),
      setReaction: (chatId, messageId, emoji) => request("telegram.setReaction", { chatId, messageId, emoji }),
      sendMessage: (chatId, text, options = {}) =>
        request("telegram.sendMessage", Object.assign({}, options, { chatId, text })),
    }),
    files: Object.freeze({
      readFile: () => request("files.readFile"),
      writeFile: (base64, suggestedName = "export.bin") => request("files.writeFile", { base64, suggestedName }),
      readText: () => request("files.readText"),
      writeText: (text, suggestedName = "export.txt") => request("files.writeText", { text, suggestedName }),
    }),
    money: Object.freeze({
      getBalance: (currency = "stars") => request("money.getBalance", { currency }),
    }),
    miniApps: Object.freeze({
      open: (botId, options = {}) => request("miniApps.open", Object.assign({}, options, { botId })),
    }),
    http: Object.freeze({ get: url => request("http.get", { url }) }),
    storage: Object.freeze({
      get: key => request("storage.get", { key }),
      set: (key, value) => request("storage.set", { key, value }),
      remove: key => request("storage.remove", { key }),
    }),
    timers: Object.freeze({
      every: (name, seconds) => request("timers.every", { name, seconds }),
      cancel: name => request("timers.cancel", { name }),
    }),
    ui: Object.freeze({
      addAction: (id, title) => request("ui.addAction", { id, title }),
    }),
  });
  Object.defineProperty(global, "__jellyStart", { value: (activate, settings) => {
    if (typeof activate !== "function") throw new Error("DEFAULT_EXPORT_REQUIRED");
    Promise.resolve(activate(api, Object.freeze(settings))).catch(error => {
      post({ type: "fatal", error: String(error).slice(0, 500) });
    });
  } });
  Object.defineProperty(global, "__jellyDispatch", { value: frame => {
    if (frame.type === "result") {
      const operation = pending.get(frame.id);
      if (!operation) return;
      pending.delete(frame.id);
      if (frame.error) {
        operation.reject(apiError(frame.error, frame.retryAfter || 0));
      } else operation.resolve(frame.value);
    } else if (frame.type === "event") {
      for (const handler of [...(handlers.get(frame.event) || [])]) {
        Promise.resolve().then(() => handler(frame.data)).catch(report);
      }
    }
  } });
})(this);
