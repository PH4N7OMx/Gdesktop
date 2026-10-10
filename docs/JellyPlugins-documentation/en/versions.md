# Versions and compatibility

## API 1

JellyPlugins uses TypeScript/JavaScript and `.jelly` packages. Legacy `.jellyplugin` files can still be imported.

- New message events, text sends, HTTPS GET, JSON storage, timers and UI actions.
- Separate permissions for joining channels, callback buttons, attachment copying, editing outgoing messages, reactions and history.
- Read-only installed JavaScript viewer.
- Russian and English documentation.

`apiVersion: 1` identifies the client contract; `version` identifies a plugin release. New permission fields are optional. Existing packages receive no additional grants automatically. Updates require approval again.

Python plugins for ExteraGram are not compatible. Pack TypeScript with the SDK before installing.
