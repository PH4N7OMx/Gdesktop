# API 1 reference

The default export receives `jelly` and read-only JSON `settings`. Promise methods may reject with a [structured error](errors.md). Permissions are enforced by the client even for forged SDK requests.

## Events

`jelly.on(event, handler)` returns an unsubscribe function. Up to 32 handlers per event.

- `message.new`: `chatId`, `messageId`, `senderId`, `text` (up to 16384 characters), `date` (Unix seconds). Only new incoming non-service server messages in `readChats`. Outgoing sends and ordinary history reads do not trigger it. Catch-up updates after offline time may include older incoming messages; filter dates/IDs if needed.
- `timer`: `name` and `timestamp`.
- `action`: `id` of a registered plugin button.

Async handlers can overlap. Serialize operations that share state. Handler errors are logged; activation errors stop the plugin.

## Telegram

| Method | Permission | Result |
|---|---|---|
| `sendMessage(chatId, text, { replyTo?, silent? })` | `sendChats` | `{ accepted, messageId }` |
| `joinChannel(chatId)` | `joinChannels` | `true` |
| `getHistory(chatId, { beforeId?, limit? })` | `historyChats` | `HistoryMessage[]` |
| `clickButton(chatId, messageId, row, column)` | `botChats` | `{ text, alert }` |
| `sendAttachment(chatId, sourceChatId, messageId, { silent? })` | `attachmentChats` for source; `sendChats` for destination | `{ accepted, messageId }` |
| `editMessage(chatId, messageId, text)` | `editChats` | `true` |
| `setReaction(chatId, messageId, emoji)` | `reactionChats` | `true` |

IDs are strings in Bot API format. Chats must be known to the session. Text sends accept 1–4096 characters without HTML/Markdown parsing. Reply IDs belong to the same chat. Paid Stars sends are blocked. A successful server reply returns accepted; messageId can be 0 if no matching ID is returned. Acceptance does not mean read by the recipient.

Joining supports known channels/supergroups by ID. Invite links, paid joining and interactive flows are unavailable.

History returns newest first, default 20 and maximum 100 messages. `beforeId` is an exclusive upper ID bound; 0 reads the latest page. Each message contains the event fields plus `hasAttachment` and `buttons: { row, column, text }[]` for ordinary callback buttons. No full profiles, file bytes or session credentials are exposed. History does not emit new-message events or mark messages as read.

Callback row/column indices start at zero. The message must be loaded: open its chat or read history with an additional grant. Only ordinary inline callback buttons are supported. Password, URL, payment, WebView, phone and location buttons are unavailable. Bot replies do not automatically open links.

Attachments copy a photo/document and its caption from a loaded Telegram message without the forwarding author. Open the source chat or load history first. Protected forwarding and paid sends are rejected. Device files and HTTP downloads are not supported by this method. Telegram enforces destination media permissions.

Editing changes your own loaded outgoing message text or attachment caption (1–4096 characters), subject to Telegram's edit window and rights. Reactions replace the selection with an ordinary emoji; an empty string clears reactions. Paid/custom emoji are unsupported.

All Telegram calls share rate limits, including history. Leave at least 3 seconds between calls and handle RATE_LIMIT/retryAfter. No automatic retry is performed.

## HTTP

`await jelly.http.get(url)` → `{ status, text }`. Exact `httpHosts` grant, HTTPS GET on port 443 only. Up to 20 requests/minute, 1 MiB UTF-8 response and a 15-second timeout. Redirects, credentials in URLs, private/local addresses, POST and arbitrary headers are blocked. No browser cookies are passed. Requests bypass the Telegram account proxy.

## Storage

`storage.get(key)` returns JSON or null, `set(key, value)` and `remove(key)` return true. Requires `storage`. Keys: 1–128 characters; total JSON: 64 KiB. Account/plugin isolation; data survives restart and update. Not separately encrypted. A get/set pair is not a transaction.

## Timers

`timers.every(name, seconds)` and `cancel(name)` return true and require `timers`. Up to 16 timers; names 1–64 characters; period 10–86400 seconds. Re-registering replaces the timer. Register again on activation. Missed ticks are not replayed; no work runs while the client is closed or the computer sleeps.

## Interface and log

`ui.addAction(id, title)` requires `ui` and returns true. Up to 8 actions; ID 1–64 characters, title 1–80. Reusing an ID updates the title. Actions appear on the JellyPlugins page.

`jelly.log(text)` requires no permission. At most 500 characters per entry; the last 100 remain in memory. Avoid logging secrets or complete conversations.

## Files, balance and Mini Apps

See the [dedicated guide](account-and-files.md): `files.readText`, `files.writeText`, `money.getBalance`, `miniApps.open`.
