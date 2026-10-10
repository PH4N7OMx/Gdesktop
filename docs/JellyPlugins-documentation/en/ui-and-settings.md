# Message menus, dialogs and settings

## Message actions

Declare `menuChats: ["123456789"]` and register `await jelly.ui.addMessageAction("inspect", "Inspect message")` on activation. The client adds a plugin-labelled action to the context menu of a single server message in those chats. Service messages and multiple/text selections have no plugin actions. No additional `ui` or `readChats` permission is needed.

`jelly.on("message.action", ({ id, message }) => { ... })` runs only when the user clicks that action. The message contains `chatId`, `messageId`, `senderId`, `text` (up to 16384 characters), `date`, `outgoing` and `hasAttachment`. It contains no raw client objects or local attachment bytes. Permission and the current loaded message are checked again at click time. Stopping or replacing the plugin invalidates actions in an already-open menu. Deleted/unavailable messages produce no event.

Up to eight message actions per plugin and sixteen across one menu are displayed. IDs are 1–64 characters; titles are 1–80. Re-registering an ID replaces its title. `ui.removeMessageAction(id)` removes it; register again on activation. `menuChats` gives access to clicked messages, not a background subscription or history. Sending still requires `sendChats`, and HTTP access requires `httpHosts`. Explain any data transmission in the plugin description.

`ui.removeAction(id)` similarly removes a button registered with `ui.addAction` and requires `ui`. Removing an absent ID succeeds.

## Notifications and confirmation

`uiDialogs: true` enables these methods. Its grant is unchecked initially.

- `await jelly.ui.showToast(text)` returns `true` after requesting an in-client toast with the plugin name. The input accepts 1–2000 characters; at most 500 are displayed. This does not create a Windows notification. All plugins on an account share a five-second toast interval.
- `await jelly.ui.confirm(title, text)` shows a text-only dialog labelled with the plugin name. Title: 1–80 characters; text: 1–2000. OK resolves `true`; Cancel, Escape or closing resolves `false`. The dialog cannot grant extra API permissions or approve Telegram payments.

Dialogs share the account's interaction slot and 30-second interval with file pickers and Mini App openings. An open file picker or plugin dialog gives `INTERACTION_BUSY`. The ordinary operation timeout is suspended while waiting for the user. Disabling closes the dialog and stops its worker. A missing account window returns `UI_UNAVAILABLE_OR_INVALID_TEXT`; a malformed title returns `INVALID_DIALOG_TITLE`. No automatic retry occurs.

## Settings forms

An optional `settingsSchema` array in the manifest creates a normal form instead of requiring users to type JSON. No runtime UI permission is needed to configure an installed plugin. The author supplies labels and option text; client buttons and notices use the client language.

```json
"settingsSchema": [
  {"key":"enabled","label":"Enabled","type":"boolean","default":true},
  {"key":"prefix","label":"Prefix","type":"string","default":"Preview","maxLength":40},
  {"key":"maxChars","label":"Maximum characters","type":"number","default":120,"min":1,"max":500},
  {"key":"mode","label":"Output","type":"select","default":"Preview","options":["Preview","Log"]}
]
```

At most 16 fields and 4 KiB of compact UTF-8 schema JSON. Every field needs a unique `key`, nonempty `label` (up to 80 characters), `type` and a valid `default`. Keys: 1–64 ASCII characters, lowercase letter first, then letters, digits, underscores, dots or hyphens. Unknown field properties and types are rejected.

- `boolean`: checkbox.
- `string`: text field; `maxLength` defaults to 512 and may be 1–2000.
- `number`: finite numeric value; defaults to the range -1e9 through 1e9. Optional `min`/`max` must be finite and inside those bounds. Use a decimal point.
- `select`: single-choice controls; 1–8 unique nonempty strings, each up to 80 characters. Default must be an option.

Missing values are filled with defaults. Saved values persist by account and plugin ID; updates preserve them. An incompatible saved value must be corrected before the plugin can start. Validation applies to the form and advanced JSON editor. Undeclared JSON keys are preserved; the total settings limit remains 8 KiB. Saving stops the plugin; enable it again to receive updated read-only `settings`. Plugins without a schema retain the JSON editor.

## Try it

Run `npm run pack:tools` in the SDK. Replace the sample `menuChats` ID before packing. Install `examples/message-tools/dist/jelly.message-tools.jelly`, configure its form and approve message-menu/dialog access. Right-click a message in the approved chat and choose `Message Tools · Inspect message`. The example reads only that clicked message; it performs no Telegram sends or HTTP requests.
