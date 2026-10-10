# Installation and management

1. Open GummyGram Settings → JellyPlugins → Install JellyPlugin.
2. Select a `.jelly` package, up to 1 MiB. Legacy `.jellyplugin` files are accepted.
3. Review the name, author, description and version. The author's name is not a verified signature.
4. Save the package, then open its card and choose Enable.
5. Approve only the requested chats, websites and features you need. Installing does not run code.

The Documentation button opens Russian or English according to the client language. View code displays the installed JavaScript with scrolling and copying; viewing does not execute it. Bundled code may differ from original TypeScript.

## Configure

Enter the author's JSON settings in the plugin card. Settings are limited to 8 KiB. Saving stops the plugin; enable it again to apply changes. Settings cannot expand approved permissions.

## Stop, remove and update

Disable revokes access and cancels pending work. Stop all applies to the current account. A server request already submitted may still complete; do not blindly repeat an operation with an unknown outcome.

Removal deletes the package, settings and plugin storage. Account rate history is retained. Runtime copies are not automatically cleaned up. Importing an updated package with the same ID stops the previous instance, retains settings/storage/rate history and requires approval again.

After a client crash, enable plugins manually. The log keeps the last 100 entries in memory. Registered action buttons appear under the plugin while enabled.

To change code, edit sources in the SDK, pack again and install the new package. An integrated TypeScript editor is not currently available.
