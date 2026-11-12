# Lifecycle

The default export is `(api) => void | cleanup`; activation and cleanup may be async. Ready follows completion of activation and its Promise. Cleanup has a bounded stop deadline.

Activation can read settings/storage and register intervals. `api.settings` is a readonly JSON snapshot; saving changed settings restarts an enabled instance. There is no `onMessage`, arbitrary timer API or Node.js event loop.

Disable, error, update and uninstall end the instance. Late replies from old generations never reach new instances. Do not rely on new API calls during cleanup after stop. An external watchdog kills a stuck host.

Normal shutdown preserves the enabled revision for next startup. Unexpected shutdown requires manual enabling. Missed interval events are not replayed.
