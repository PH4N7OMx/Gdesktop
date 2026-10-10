# ExteraGram plugin comparison

Inspected the user-provided ExteraGram-src checkout statically. This tree contains decompiled Java and compiled Cython SDK modules; it was not built or executed. No code or Android binaries were imported into GummyGram.

Evidence: `messenger/plugins/hooks/PluginsHooks.java` exposes request, response, updates and send-message hooks. `PythonPluginsEngine.parsePySettingDefinitions` handles switch, input, edit_text, selector, header, text, divider and custom views. `PluginsMenuWrapper` uses menu location, context and click callbacks. The SDK archives in `app/src/main/assets/plugins_pysdk` contain `base_plugin`, `client_utils`, `ui.bulletin` and `ui.alert`; their static symbols include hook registration, menu registration, media sends, bulletins and dialog builders. Symbol names alone do not verify live Android behavior.

| Area | JellyPlugins decision |
|---|---|
| Message menu and callback context | Implemented scoped `menuChats` and `message.action`; no raw objects |
| Settings controls | Implemented manifest schema with boolean/string/number/select; retained advanced JSON |
| Bulletins/dialogs | Implemented named in-client toasts and text-only confirmation with separate grant |
| Removing menu/buttons | Implemented idempotent removal APIs |
| Java reflection/Xposed and raw TL hooks | Not ported across the isolated worker boundary |
| Arbitrary Telegram requests | No raw request interface; use specific validated methods |
| Upload local media | Candidate follow-up via user-selected upload flow and destination grants; not implemented |
| Composer transformations | Candidate follow-up with narrow outgoing-text hooks and explicit consent; not implemented |
| Profile/drawer menu extensions | Candidate follow-up with scoped context; not implemented |
| pip and native dependency installation | Not adopted; dependencies must bundle as JavaScript |
| Dev server/hot reload | Not implemented; SDK pack/type checks remain explicit |

Native client compilation and runtime UI behavior are unverified. SDK tests verify packaging, invalid schemas, event delivery and request/result semantics, not native permissions or dialogs.
