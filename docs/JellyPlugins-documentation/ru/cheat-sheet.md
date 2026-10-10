# Шпаргалка API

Все методы доступны через `jelly` в функции активации. Чаты задаются строками. Методы с `Promise` нужно ожидать через `await` и обрабатывать ошибки.

| Задача | Вызов | Разрешение |
|---|---|---|
| Подписаться на новые сообщения | `jelly.on("message.new", handler)` | `readChats` |
| Подписаться на таймер / кнопку | `jelly.on("timer", handler)` / `jelly.on("action", handler)` | Регистрация требует `timers` / `ui` |
| Отправить текст | `telegram.sendMessage(chatId, text, { replyTo?, silent? })` | `sendChats` |
| Вступить в канал | `telegram.joinChannel(chatId)` | `joinChannels` |
| Прочитать историю | `telegram.getHistory(chatId, { beforeId?, limit? })` | `historyChats` |
| Нажать callback-кнопку | `telegram.clickButton(chatId, messageId, row, column)` | `botChats` |
| Скопировать вложение Telegram | `telegram.sendAttachment(chatId, sourceChatId, messageId, { silent? })` | `attachmentChats` у источника, `sendChats` у получателя |
| Изменить исходящий текст / подпись | `telegram.editMessage(chatId, messageId, text)` | `editChats` |
| Поставить / снять реакцию | `telegram.setReaction(chatId, messageId, emoji)`; снять — `""` | `reactionChats` |
| Получить текст по HTTPS | `http.get(url)` | `httpHosts` |
| Прочитать JSON-значение | `storage.get(key)` | `storage` |
| Сохранить / удалить значение | `storage.set(key, value)` / `storage.remove(key)` | `storage` |
| Повторяющийся таймер | `timers.every(name, seconds)` / `timers.cancel(name)` | `timers` |
| Кнопка на странице JellyPlugins | `ui.addAction(id, title)` | `ui` |
| Запись в журнал | `jelly.log(text)` | Не нужно |

Имена без `jelly.` в таблице сокращены: например, `telegram.sendMessage` означает `jelly.telegram.sendMessage`.

## Что помнить

- `jelly.on` возвращает функцию отписки.
- История возвращается от новых сообщений к старым; следующий запрос использует последний ID как `beforeId`.
- Индексы кнопок начинаются с нуля. Кнопки, исходящие сообщения для редактирования и источники вложений должны быть загружены в клиент.
- Все операции Telegram расходуют общий лимит. Между ними не меньше 3 секунд; учитывай `retryAfter`.
- Таймеры и кнопки регистрируются при каждой активации. Хранилище и настройки переживают перезапуск.
- `jelly` не предоставляет Node.js, DOM, произвольный доступ к файлам или системным командам.

Параметры, результаты и ограничения: [справочник API](api.md). Для проблем с запуском: [отладка](debugging.md).

[Дополнительные API](account-and-files.md): чтение и запись выбранных файлов, баланс Stars/Telegram TON, открытие Mini Apps.
