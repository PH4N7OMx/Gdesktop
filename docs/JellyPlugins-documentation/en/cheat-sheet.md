# API cheat sheet

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

See [API](api.md) for limits and [permissions](security.md) for access rules. `jellyplugins/v2/sdk.d.ts` is generated from `contract.json`. Handle denial and `OUTCOME_UNKNOWN`; never automatically retry a send with unknown outcome.
