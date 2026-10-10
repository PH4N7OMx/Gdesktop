# JellyPlugins for GummyGram

JellyPlugins runs TypeScript/JavaScript plugins through a scoped API in the GummyGram client. API version: **1**. Packages use **.jelly**.

Start with [installation](installation.md) or [your first plugin](quickstart.md). See the [API reference](api.md), [manifest](manifest.md), [permissions](security.md), [examples](examples.md) and [errors and limits](errors.md).

## Features

| Feature | Support |
|---|---|
| New incoming messages | Text and basic IDs in approved chats |
| Text, replies and silent sends | Yes |
| Join channels and callback buttons | Separate permissions |
| Attachments | Copy existing Telegram photos/documents |
| Edit messages and reactions | Separate permissions |
| Older history | Paged reads in approved chats |
| HTTPS GET, JSON storage, timers and action buttons | Yes |
| Code viewer | Installed JavaScript, read-only |

Execution currently requires Windows AppContainer. Other platforms can store a package but cannot enable it. Users do not need Node.js or npm; developers use them to build packages.

[Русская версия](../ru/README.md)
