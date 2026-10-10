# Permissions and security

Plugins run as untrusted JavaScript in a separate Windows AppContainer process. The worker has no direct network access, Node.js, DOM, system commands, native modules, direct device file access, session keys, login codes or 2FA password. It talks to the client through JSON; the client checks permissions and parameters on each operation.

Approve chats and exact HTTPS domains individually. Storage, timers and interface actions have separate grants. New Telegram actions require separate chat scopes. A plugin update always requires renewed approval. Disabling stops execution and revokes access; signing out stops that account's plugins.

Reading new messages, history, selected files or balances together with HTTP access allows a plugin to send those data to approved websites. Review both grants together. Copying attachments can also disclose their captions and contents to the destination chat.

Plugin JSON storage is isolated by account and plugin ID, but is not separately encrypted. Avoid storing secrets. The author field is not verified and the API provides no audited marketplace or antivirus scanning.

The client copies its own executable and DLLs to a versioned `GummyGram/plugin-runtime` directory under Windows local application data for AppContainer execution. This uses additional disk space; old runtime copies are not automatically removed. It does not grant access to the original client folder or `tdata`.

Isolation does not remove all engine, OS or host vulnerabilities. Keep the client updated and install plugins from sources you trust.

Selected files, balance reads and Mini App opening require [separate grants](account-and-files.md). File paths are never exposed, and every read/save asks the user to select a file. File/balance/Mini App grants start unchecked.
