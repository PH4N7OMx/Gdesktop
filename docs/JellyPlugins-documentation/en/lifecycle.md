# Plugin lifecycle

`TypeScript → pack → .jelly → install → permissions → activation → events`

## Activation

Installation stores a package. Enabling starts an isolated process and calls its default export with `jelly` and `settings`. Register handlers, action buttons and timers on every activation. Activation failure stops the plugin; handler failures are logged.

Registered handlers receive `message.new`, `timer` and `action` events. Async work can overlap. `jelly.on(...)` returns an unsubscribe function.

## Where to keep state

| Data | After stop / restart | Purpose |
|---|---|---|
| JavaScript variable | Lost | Current queue or busy flag |
| `settings` | Retained | User JSON configuration, per account and plugin ID |
| `storage` | Retained | Counters, processed IDs, workflow state |
| Timers, handlers and buttons | Register again | Work for the current instance |
| Log | Not written to disk | Last 100 entries in client memory |
| Telegram budgets and FloodWait | Retained | Limits across restart and update |

The same ID on two accounts does not share storage or permissions. Settings cannot expand manifest grants.

## Changes and stopping

- Saving settings stops the plugin; enable it again.
- A new package with the same ID replaces code, retains settings/storage and requires access approval again.
- Disabling revokes access and stops timers and pending work.
- Removal deletes package, settings and storage; the shared account budget remains.
- After a client crash, check status and enable manually.

Do not automatically resend when the outcome is unknown. Keep processed IDs and inspect history when needed. Treat website and message content as workflow input.

Next: [API](api.md), [permissions](security.md), [debugging](debugging.md).
