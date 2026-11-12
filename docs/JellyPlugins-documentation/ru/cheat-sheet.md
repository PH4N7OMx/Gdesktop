# Шпаргалка API

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

Ограничения — в [API](api.md), правила доступа — в [разрешениях](security.md). Типы `jellyplugins/v2/sdk.d.ts` генерируются из `contract.json`. Обрабатывай отказ пользователя и `OUTCOME_UNKNOWN`; неизвестный исход отправки нельзя автоматически повторять.
