export type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };
export interface Methods {
  "telegram.sendText": { params: { chatId: string; text: string; silent?: boolean }; result: { messageId: string } };
  "telegram.readHistory": { params: { chatId: string; limit?: number }; result: ReadonlyArray<{ id: string; text: string }> };
  "schedule.every": { params: { id: string; seconds: number; first?: "after-period" | "immediate" }; result: boolean };
  "schedule.cancel": { params: { id: string }; result: boolean };
  "storage.get": { params: { key: string }; result: JsonValue };
  "storage.set": { params: { key: string; value: JsonValue }; result: boolean };
}
export interface JellyApi {
  readonly telegram: {
    sendText(params: Methods["telegram.sendText"]["params"]): Promise<Methods["telegram.sendText"]["result"]>;
    readHistory(params: Methods["telegram.readHistory"]["params"]): Promise<Methods["telegram.readHistory"]["result"]>;
  };
  readonly schedule: {
    every(id: string, seconds: number, handler: () => void | Promise<void>, options?: { first?: "after-period" | "immediate" }): Promise<void>;
    cancel(id: string): Promise<void>;
  };
  readonly storage: { get(key: string): Promise<JsonValue>; set(key: string, value: JsonValue): Promise<boolean> };
  readonly log: { info(message: string): void; error(message: string): void };
  readonly settings: Readonly<{ [key: string]: JsonValue }>;
}
export type Plugin = (api: JellyApi) => void | (() => void | Promise<void>) | Promise<void | (() => void | Promise<void>)>;
