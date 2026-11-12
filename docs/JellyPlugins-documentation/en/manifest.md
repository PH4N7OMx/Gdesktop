# Manifest and package

```json
{
  "id": "jelly.hello",
  "name": "Hello JellyPlugins v2",
  "version": "2.0.0",
  "apiVersion": 2,
  "capabilities": []
}
```

Required: `id`, `name`, `version`, `apiVersion: 2`, `capabilities`. Optional strings: `author`, `description`. Unknown fields are rejected. ID: `[a-z][a-z0-9.-]{2,63}`; version: three dotted integers; name: up to 80 characters.

Categories `telegram.send`, `telegram.read`, `schedule`, `storage` declare intent and do not grant access. Author metadata is not verified publisher identity.

The JSON container contains `formatVersion: 2`, `manifest` and one ESM bundle in `code`, up to 1 MiB. External imports, bytecode, archives, resources and top-level await are unsupported. SHA-256 identifies content revision, not authenticity.
