# Files, balance and Mini Apps

These are scoped client APIs, not native FFI. No DLLs, unrestricted filesystem, account credentials or command execution are exposed. Each feature requires a separate grant before activation; its checkbox is initially off.

## Selected files

`files.readText()` requires `fileRead` and returns `{ name, text }`. `files.writeText(text, suggestedName = "export.txt")` requires `fileWrite` and returns `{ name }`. The client shows a file picker with the plugin name for every call. A save uses the normal overwrite confirmation and atomic replacement. Cancellation rejects with `USER_CANCELLED`.

Each file is limited to 1 MiB. Text methods require valid UTF-8. The full path is never returned. The API accepts no input path, directory grants or background file access. The current client's `tdata` directory and symbolic links are rejected. File selection has no 30-second operation timeout. Disabling the plugin closes its picker. A large result can exceed the 2 MiB JSON frame limit after escaping and fail with `RESPONSE_LIMIT`.

The filename suggestion must be a simple name of 1–120 characters, without separators or Windows-reserved characters. The user chooses the destination and can replace an existing file. This is separate from copying Telegram attachments; `sendAttachment` does not upload local files.

For binary files, `files.readFile()` returns `{ name, base64 }` and `files.writeFile(base64, suggestedName = "export.bin")` returns `{ name }`. They use the same permissions and picker. Base64 must be canonical with padding; empty content is valid. The size limit applies to decoded bytes.

## Balance

`money.getBalance(currency = "stars")` requires `moneyRead`. Currency is `"stars"` or `"ton"`. Returns `{ currency, whole: string, nanos: number }` from the current account's Telegram balance endpoint. Keep `whole` as a string to preserve precision; `nanos` represents billionths.

This exposes the Telegram Stars balance and Telegram's own TON balance when available. It does not connect to the third-party Wallet app or an arbitrary TON address. Telegram may reject an unavailable balance. No transaction history, keys, purchases, transfers or withdrawals are exposed. Balance reads share the existing Telegram operation budget.

## Telegram Mini Apps

`miniApps.open(botId, { startParam? })` requires `webviewBots: ["123456789"]`. Use a positive bot ID known to the session. Only bots with a main Mini App are supported. `startParam` is at most 512 letters, digits, underscores or hyphens.

The client uses its regular Telegram Mini App flow and confirmation. The Mini App receives the normal Telegram profile data. Plugins do not receive initData, cookies, page contents, scripts or payment controls. The result `{ requested: true }` means opening was requested; it does not confirm loading, login or payment. The Mini App remains an independent client window after the plugin stops.

File pickers and Mini App openings share one interaction slot per account and at least 30 seconds between requests. Another open file picker or plugin confirmation dialog rejects with `INTERACTION_BUSY`. Otherwise `RATE_LIMIT` includes `retryAfter`. Mini App openings also use the Telegram operation budget.

## Example permissions

```json
{"fileRead":true,"fileWrite":true,"moneyRead":true,"webviewBots":["123456789"],"ui":true}
```

Ask for only the features you use. Trigger file selection from an explicit plugin action rather than automatically on activation. Combining reads with HTTP or outgoing messages can disclose selected content or balances; explain this in the plugin description.
