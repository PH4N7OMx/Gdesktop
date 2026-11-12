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

Вызовы асинхронны, кроме журнала и снимка settings. SDK и клиент независимо проверяют параметры; неизвестные поля отклоняются. ID чата — строка: положительный пользователь, отрицательная группа или Telegram channel/supergroup ID с префиксом `-100`. Получатель должен быть загружен клиентом.

Текст: 1–4096 UTF-16 единиц, `silent=false` по умолчанию. Без разметки, entities, вложений, reply и предпросмотра ссылок. Платные Stars-сообщения отклоняются. История: `limit` 1–100, по умолчанию 20; возвращаются ID/текст обычных сообщений. Чтение требует отдельного разрешения до раскрытия данных.

Интервал: 60–604800 секунд, максимум 16 расписаний; ID уникален до отмены. `options.first`: `after-period` (по умолчанию) или `immediate`. Async-handler не перекрывает себя; пропущенные тики не догоняются пачкой.

Storage: ключ `[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}`, JSON суммарно до 64 КиБ, глубина до 32, конечные числа и обычные JSON-значения без циклов. Данные разделены по аккаунту и плагину.
