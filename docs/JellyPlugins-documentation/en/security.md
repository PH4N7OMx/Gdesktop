# Permissions and isolation

| Mode | Behavior |
|---|---|
| Ask | Every protected operation needs a decision |
| Saved rules | Matching allows execute; unfamiliar actions ask |
| Full access | Implemented client API without new questions |
| Inherit | Per-plugin only: use global mode |

Explicit saved denials override full mode too. One-time choices do not create rules. Save exact normalized parameters, or explicitly allow varying text in one chat with the same `silent` setting. A different chat/notification setting needs another rule. History uses separate rules. Rules are bound to account, plugin, package revision and API contract.

Saving an allow in Ask mode switches that plugin to Saved rules. Global full mode affects inheriting plugins and future installations; confirmation states this scope. Per-plugin mode overrides global mode.

Each instance has a separate host: Windows AppContainer/Job Object, Linux no_new_privs/seccomp, macOS independently signed App Sandbox. No unsandboxed fallback. Native validation remains required before release; source code alone is not sandbox security proof.
