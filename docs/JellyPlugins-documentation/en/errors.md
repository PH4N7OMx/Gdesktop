# Errors and limits

Promise errors contain `code`, `message`, `stage`, and FloodWait `retryAfter`. Handle user denial and network failure in async handlers.

| Code | Meaning |
|---|---|
| `PERMISSION_DENIED` | User or saved rule denied the action |
| `PERMISSION_REVOKED` | Access revoked during reading |
| `REQUEST_EXPIRED` | Deadline expired |
| `OUTCOME_UNKNOWN` | Send may have succeeded; do not retry automatically |
| `CHAT_UNAVAILABLE` / `CHAT_NOT_WRITABLE` | Unloaded/unavailable peer or Stars required |
| `FLOOD_WAIT` / `RATE_LIMIT` | Respect delay/operation limits |
| `RATE_STATE_UNAVAILABLE` | Corrupted persisted rate state |
| `STORAGE_WRITE_FAILED` / `STATE_WRITE_FAILED` | Persistence failed |
| `METHOD_UNAVAILABLE` / `INVALID_PARAMETER` | Unsupported method/parameters |

JS heap 64 MiB, stack 512 KiB, turn 250 ms, 10000 microtasks/turn, 32 pending calls, 16 schedules. The native process has separate limits. Up to 20 Telegram operations/hour/plugin and 60/account; full mode cannot bypass limits. API deadline: 30 seconds; approval: 5 minutes. Launch/isolation errors appear in status and log.
