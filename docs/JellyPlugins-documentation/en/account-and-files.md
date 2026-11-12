# Account and file access

Text sending and history reading use the checked Telegram bridge. Arbitrary MTProto, session keys, account export, payments/balance, Mini Apps, files, attachments and HTTP are unavailable.

Full mode grants the implemented client API without repeated questions, not direct filesystem, network or OS command access. Persist local JSON with `api.storage`.

State and permissions are scoped by account and plugin. Authors and IDs are not publisher-verified; review code before enabling.
