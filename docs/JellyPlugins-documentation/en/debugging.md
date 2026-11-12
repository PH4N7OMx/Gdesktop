# Debugging

Check the new client version, API 2 package and bundled host. Inspect status and log first: installed, activating and ready are different states. Use `api.log.info/error` for diagnostics.

Windows/Linux: adjacent `jelly-runtime`; macOS: `.app/Contents/Helpers`. The main executable is not copied to AppData. A missing host needs a complete installation rather than disabling isolation.

Run SDK `npm run check` and `npm test`. Native evidence comes from `JellyPlugins v2 build and isolation` Actions and full client tests. Do not automatically send live messages for testing or log session secrets/private chats.
