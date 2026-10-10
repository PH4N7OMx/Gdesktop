# Debugging and troubleshooting

Start in the plugin card: check its status, open the action log, then repeat one specific step. Use `jelly.log()`. The client currently provides no browser console, Node.js debugger or TypeScript breakpoints.

| Symptom | Check |
|---|---|
| Package cannot be installed | JSON `.jelly` produced by the packer, up to 1 MiB; valid manifest and `apiVersion: 1` |
| Installed but inactive | Installation does not execute code. Enable and approve permissions |
| No action button | Manifest requests `ui: true`, user approved it, activation reached `ui.addAction` |
| Stops after saving settings | Settings stop execution; enable again |
| `PERMISSION_DENIED` | Requested and granted permissions include the exact chat ID / host |
| `CHAT_UNAVAILABLE` | Chat is known to this account; open it in the client |
| `BUTTON_UNAVAILABLE` / `ATTACHMENT_UNAVAILABLE` | Message loaded, supported type; attachment source and destination approved |
| `RATE_LIMIT` / `FLOOD_WAIT` | Wait `retryAfter`. History reads also consume Telegram limits |
| No new message events | Need `readChats`. The event is for incoming messages; use `getHistory` for older messages |
| Lost state during rapid events | Async handlers overlap; use a busy flag or a queue |
| Old behavior after changing TypeScript | Check types, repack, import the new `.jelly` and approve permissions |
| OS refuses execution | Windows AppContainer required; check status in the card. Other platforms cannot run plugins yet |

## Log an API failure

Use this fragment inside activation or an event handler; `chatId` and `text` are values from your workflow.

```typescript
try {
  await jelly.telegram.sendMessage(chatId, text);
} catch (cause) {
  const error = cause as PluginError;
  jelly.log(JSON.stringify({ code: error.code, retryAfter: error.retryAfter }));
}
```

For projects inside `examples/`, import the type with `import type { PluginError } from "../sdk-types";`. Avoid logging passwords or complete conversations.

## Verify a change

1. Run `npm run check` in the SDK.
2. Pack with `node scripts/pack.mjs examples/my-plugin`.
3. Import the package with the same ID to replace the previous version, then approve access.
4. Exercise one workflow and inspect its log. Start with a button or preview before sending messages.

View code shows installed JavaScript rather than original TypeScript. Use it to confirm which package is installed. Stopping cannot guarantee cancellation of a request already submitted to a server; inspect the outcome before retrying.

See [errors and limits](errors.md) for codes and [lifecycle](lifecycle.md) for persistence.
