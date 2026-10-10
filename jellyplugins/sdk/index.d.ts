export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };

export interface MessageEvent {
  chatId: string;
  messageId: number;
  senderId: string;
  text: string;
  date: number;
}

export interface PluginEvents {
  "message.new": MessageEvent;
  "message.action": { id: string; message: MessageEvent & { outgoing: boolean; hasAttachment: boolean } };
  timer: { name: string; timestamp: number };
  action: { id: string };
}

export interface PluginError extends Error {
  code: string;
  retryAfter: number;
}

export interface HistoryMessage extends MessageEvent {
  hasAttachment: boolean;
  buttons: { row: number; column: number; text: string }[];
}

export interface JellyAPI {
  readonly apiVersion: 1;
  on<E extends keyof PluginEvents>(event: E,
    handler: (data: PluginEvents[E]) => void | Promise<void>): () => void;
  log(text: string): void;
  readonly telegram: {
    joinChannel(chatId: string): Promise<boolean>;
    clickButton(chatId: string, messageId: number, row: number, column: number):
      Promise<{ text: string; alert: boolean }>;
    getHistory(chatId: string, options?: { beforeId?: number; limit?: number }):
      Promise<HistoryMessage[]>;
    sendAttachment(chatId: string, sourceChatId: string, messageId: number,
      options?: { silent?: boolean }): Promise<{ accepted: boolean; messageId: number }>;
    editMessage(chatId: string, messageId: number, text: string): Promise<boolean>;
    setReaction(chatId: string, messageId: number, emoji: string): Promise<boolean>;
    sendMessage(chatId: string, text: string,
      options?: { replyTo?: number; silent?: boolean }):
      Promise<{ accepted: boolean; messageId: number }>;
  };
  readonly files: {
    readFile(): Promise<{ name: string; base64: string }>;
    writeFile(base64: string, suggestedName?: string): Promise<{ name: string }>;
    readText(): Promise<{ name: string; text: string }>;
    writeText(text: string, suggestedName?: string): Promise<{ name: string }>;
  };
  readonly money: {
    getBalance(currency?: "stars" | "ton"): Promise<{ currency: "stars" | "ton"; whole: string; nanos: number }>;
  };
  readonly miniApps: {
    open(botId: string, options?: { startParam?: string }): Promise<{ requested: boolean }>;
  };
  readonly http: {
    get(url: string): Promise<{ status: number; text: string }>;
  };
  readonly storage: {
    get<T extends Json = Json>(key: string): Promise<T | null>;
    set(key: string, value: Json): Promise<boolean>;
    remove(key: string): Promise<boolean>;
  };
  readonly timers: {
    every(name: string, seconds: number): Promise<boolean>;
    cancel(name: string): Promise<boolean>;
  };
  readonly ui: {
    addAction(id: string, title: string): Promise<boolean>;
    removeAction(id: string): Promise<boolean>;
    addMessageAction(id: string, title: string): Promise<boolean>;
    removeMessageAction(id: string): Promise<boolean>;
    showToast(text: string): Promise<boolean>;
    confirm(title: string, text: string): Promise<boolean>;
  };
}

export type Plugin<S extends Record<string, Json> = Record<string, Json>> =
  (jelly: JellyAPI, settings: Readonly<S>) => void | Promise<void>;

export interface Permissions {
  readChats?: string[];
  sendChats?: string[];
  joinChannels?: string[];
  botChats?: string[];
  attachmentChats?: string[];
  editChats?: string[];
  reactionChats?: string[];
  historyChats?: string[];
  httpHosts?: string[];
  webviewBots?: string[];
  menuChats?: string[];
  uiDialogs?: boolean;
  fileRead?: boolean;
  fileWrite?: boolean;
  moneyRead?: boolean;
  storage?: boolean;
  timers?: boolean;
  ui?: boolean;
  maxMessagesPerHour?: number;
}

export type SettingDefinition =
  | { key: string; label: string; type: "boolean"; default: boolean }
  | { key: string; label: string; type: "string"; default: string; maxLength?: number }
  | { key: string; label: string; type: "number"; default: number; min?: number; max?: number }
  | { key: string; label: string; type: "select"; default: string; options: string[] };

export interface Manifest {
  apiVersion: 1;
  id: string;
  name: string;
  author?: string;
  description?: string;
  version: string;
  permissions: Permissions;
  settingsSchema?: SettingDefinition[];
}

export type GummyAPI = JellyAPI;
