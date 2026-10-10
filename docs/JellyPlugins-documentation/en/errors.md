# Errors and limits

Rejected API promises contain `message`, `code` and `retryAfter` in seconds. A zero retry interval means none is supplied.

| Error | Meaning |
|---|---|
| `PERMISSION_DENIED` | Method, source/destination chat or host is not approved |
| `CHAT_UNAVAILABLE` | Chat is not known to the session |
| `CHAT_UNAVAILABLE_OR_INVALID_MESSAGE` | Sending unavailable, paid message or invalid text |
| `BUTTON_UNAVAILABLE` | Message/button not loaded or unsupported button type |
| `ATTACHMENT_UNAVAILABLE` | Source not loaded, unsupported/protected media or unavailable destination |
| `MESSAGE_NOT_EDITABLE` | Message not loaded, not outgoing, expired edit window or invalid text |
| `INVALID_HISTORY_OPTIONS` | Invalid limit or beforeId |
| `INVALID_MESSAGE_ID`, `INVALID_REACTION`, `INVALID_CHANNEL` | Invalid parameter |
| `INTERACTIVE_JOIN_REQUIRED` | Joining requires an interactive flow |
| `RATE_LIMIT` | Client Telegram operation limit; wait retryAfter |
| `FLOOD_WAIT` | Telegram requires waiting; applies to all account plugins |
| `HTTP_RATE_LIMIT`, `HTTP_BODY_LIMIT`, `HTTP_REDIRECT_BLOCKED` | HTTP restriction |

Other Telegram server error codes are passed through unchanged. No method automatically retries.

| Resource | Limit |
|---|---|
| Package | 1 MiB |
| Settings / storage | 8 KiB / 64 KiB |
| Worker memory | 256 MiB, one process |
| Continuous JS execution | 2 seconds per frame |
| Event queue / pending SDK requests | 64 / 32 |
| Pending operation | 30 seconds, then the plugin stops |
| HTTP | 20 requests/minute, 1 MiB body, 15-second timeout |
| History | 1–100 messages per request, default 20 |
| Timers | 16; periods 10–86400 seconds |
| Actions / log | 8 buttons / 100 entries of 500 characters |

All Telegram operations use the plugin's `maxMessagesPerHour` budget and a shared 60/hour account budget, with at least 3 seconds between operations. Attempts count even when the server rejects them. Limits and FloodWait persist across restarts. Deleting a plugin does not clear the account budget.

Use timers instead of busy loops. Handle unknown outcomes before retrying sends or callback actions.
