# Manifest and packages

A `.jelly` package is JSON with `manifest` and `code` fields. It is not a ZIP archive. The SDK packer emits JavaScript defining `JellyPlugin.default`.

```json
{"manifest":{"apiVersion":1,"id":"author.plugin","name":"Example","author":"Author","version":"1.0.0","permissions":{"readChats":["-1001234567890"],"sendChats":["123456789"],"historyChats":["-1001234567890"],"httpHosts":["example.com"],"storage":true,"timers":true,"ui":true,"maxMessagesPerHour":20}},"code":"var JellyPlugin = ..."}
```

| Field | Requirement |
|---|---|
| `apiVersion` | Integer `1` |
| `id` | 3–64 characters; lowercase letter first, then lowercase letters, digits, dots or hyphens |
| `name` | Nonempty string, at most 80 characters |
| `author` | Optional string, at most 80 characters |
| `description` | Optional string, at most 2000 characters |
| `version` | Three numeric components, such as `1.2.0` |
| `permissions` | Required object; unknown fields rejected |

Chat scopes use strings in Bot API ID format: positive for users, negative for groups, `-100…` for channels/supergroups. Never convert IDs to JavaScript numbers. Each scope permits at most 32 IDs or hosts. Omitted permissions grant no access.

| Permission | Access |
|---|---|
| `readChats` | New incoming messages |
| `sendChats` | Text sends and attachment destinations |
| `joinChannels` | Join specified channels/supergroups |
| `botChats` | Inline callback buttons |
| `attachmentChats` | Sources of copied photos/documents |
| `editChats` | Edit outgoing messages |
| `reactionChats` | Set reactions |
| `historyChats` | Read older messages |
| `httpHosts` | Exact HTTPS hostnames |
| `storage`, `timers`, `ui` | Boolean feature permissions |
| `maxMessagesPerHour` | All Telegram operations, 1–60 per hour; default 20 |

Permissions do not imply each other. Attachment copying needs both the source scope and `sendChats` for the destination. `httpHosts` does not accept paths, ports or wildcards; `example.com` does not permit `api.example.com`.

Approval is bound to the package SHA-256 hash. This is integrity checking, not an author signature or malware verdict.
