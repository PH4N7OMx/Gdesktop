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
  storage?: boolean;
  timers?: boolean;
  ui?: boolean;
  maxMessagesPerHour?: number;
}

export interface Manifest {
  apiVersion: 1;
  id: string;
  name: string;
  author?: string;
  description?: string;
  version: string;
  permissions: Permissions;
}

export type GummyAPI = JellyAPI;
