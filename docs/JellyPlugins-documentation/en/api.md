# API 2

| API | Parameters | Result |
|---|---|---|
| `api.telegram.sendText` | `{ chatId: string, text: string, silent?: boolean }` | `{ messageId: string }` |
| `api.telegram.readHistory` | `{ chatId: string, limit?: number }` | `[{ id: string, text: string }]` |
| `api.schedule.every` | `(id, seconds, handler, options?)` | `Promise<void>` |
| `api.schedule.cancel` | `(id)` | `Promise<void>` |
| `api.storage.get` | `(key)` | JSON or `null` |
| `api.storage.set` | `(key, value)` | `boolean` |
| `api.log.info / error` | `(message)` | `void` |
| `api.settings` | Readonly JSON object | Current settings |

Calls are async except logging and the settings snapshot. SDK and client independently validate parameters; unknown fields are rejected. Chat IDs are strings: positive user, negative group, or Telegram channel/supergroup ID using the `-100` prefix. Peers must already be loaded by the client.

Text: 1–4096 UTF-16 units, default `silent=false`. No markup, entities, attachments, replies or link previews. Paid Stars messages are rejected. History: `limit` 1–100, default 20; ordinary message IDs/text only. Reading requires separate permission before content reaches the plugin.

Intervals: 60–604800 seconds, up to 16 schedules; IDs unique until cancel. `options.first`: `after-period` (default) or `immediate`. Async handlers never overlap themselves; missed intervals never burst.

Storage keys: `[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}`. Total stored JSON up to 64 KiB, depth up to 32, finite numbers and plain JSON values without cycles. State is scoped by account and plugin.
