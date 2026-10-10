# API cheat sheet

Use `jelly` inside the activation function. Chat IDs are strings. Await Promise methods and handle rejected operations.

| Task | Call | Permission |
|---|---|---|
| Subscribe to incoming messages | `jelly.on("message.new", handler)` | `readChats` |
| Subscribe to timer / action | `jelly.on("timer", handler)` / `jelly.on("action", handler)` | Registration requires `timers` / `ui` |
| Send text | `telegram.sendMessage(chatId, text, { replyTo?, silent? })` | `sendChats` |
| Join a channel | `telegram.joinChannel(chatId)` | `joinChannels` |
| Read history | `telegram.getHistory(chatId, { beforeId?, limit? })` | `historyChats` |
| Press a callback button | `telegram.clickButton(chatId, messageId, row, column)` | `botChats` |
| Copy a Telegram attachment | `telegram.sendAttachment(chatId, sourceChatId, messageId, { silent? })` | `attachmentChats` for source, `sendChats` for destination |
| Edit outgoing text / caption | `telegram.editMessage(chatId, messageId, text)` | `editChats` |
| Set / clear a reaction | `telegram.setReaction(chatId, messageId, emoji)`; clear with `""` | `reactionChats` |
| Fetch HTTPS text | `http.get(url)` | `httpHosts` |
| Read a JSON value | `storage.get(key)` | `storage` |
| Save / remove a value | `storage.set(key, value)` / `storage.remove(key)` | `storage` |
| Repeat a timer | `timers.every(name, seconds)` / `timers.cancel(name)` | `timers` |
| Add a JellyPlugins action button | `ui.addAction(id, title)` | `ui` |
| Write a log entry | `jelly.log(text)` | None |

The table omits the `jelly.` prefix for brevity: `telegram.sendMessage` means `jelly.telegram.sendMessage`.

## Remember

- `jelly.on` returns an unsubscribe function.
- History is newest first; pass the last message ID as `beforeId` for the next page.
- Button indices are zero-based. Callback messages, outgoing messages to edit and attachment sources must be loaded in the client.
- All Telegram operations share rate limits. Leave at least 3 seconds between them and honor `retryAfter`.
- Register timers and buttons on every activation. Storage and settings survive restarts.
- No Node.js, DOM, unrestricted filesystem or system commands are provided.

For complete parameters, return values and restrictions, see the [API reference](api.md). For startup problems, see [debugging](debugging.md).

[Additional APIs](account-and-files.md): selected text files, Stars/Telegram TON balance and Mini App opening.
