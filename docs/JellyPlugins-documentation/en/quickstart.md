# Your first plugin

Get the `jellyplugins` folder from the repository or `JellyPlugins-sdk.zip`. Install Node.js with npm and open a terminal in the SDK folder.

```powershell
npm ci --ignore-scripts
npm run pack:hello
```

Install `examples/hello/dist/jelly.hello.jelly` through GummyGram → JellyPlugins. The example adds an action button, stores a counter and logs its value.


## Check the result

Enable the installed example and approve `ui` and `storage`. The supplied sample adds a button labeled `Проверить плагин` (Check plugin). Press it and open the action log: expect `Кнопка нажата 1 раз` (Button pressed once). Further presses increase the persisted counter.

If nothing appears, open [troubleshooting](debugging.md).

## Create a plugin

Copy `examples/hello` to `examples/my-plugin`. Set a unique ID in `manifest.json` and export an activation function from `index.ts`.

```typescript
import type { Plugin } from "../sdk-types";
const activate: Plugin = async jelly => {
  await jelly.ui.addAction("hello", "Say hello");
  jelly.on("action", async ({ id }) => {
    if (id !== "hello") return;
    const count = (await jelly.storage.get<number>("count") ?? 0) + 1;
    await jelly.storage.set("count", count);
    jelly.log(String(count));
  });
};
export default activate;
```

```json
{"apiVersion":1,"id":"my.first-plugin","name":"My first plugin","version":"1.0.0","permissions":{"ui":true,"storage":true}}
```

```powershell
npm run check
node scripts/pack.mjs examples/my-plugin
```

Use relative imports for additional files. All dependencies must bundle into plain JavaScript. Node.js, DOM and native modules are unavailable in plugins. The client executes packaged JavaScript rather than TypeScript source.

Catch asynchronous errors and inspect `jelly.log()` in the plugin card. Register handlers inside activation rather than sending messages at module load time.

## Project layout

```text
jellyplugins/
├── sdk/index.d.ts          API types
├── scripts/pack.mjs        packer
└── examples/
    ├── sdk-types.ts        types import for examples
    └── my-plugin/
        ├── manifest.json  name, version and permissions
        ├── index.ts       source
        └── dist/*.jelly    installable package
```

## SDK commands

| Command | Result |
|---|---|
| `npm run check` | Type checking without creating a package |
| `node scripts/pack.mjs examples/my-plugin` | Pack your plugin |
| `npm run pack:hello` | Button example |
| `npm run pack:feed` | RSS → Telegram example |
| `npm run pack:giveaways` | Message observation example |
| `npm test` | SDK and example checks |

Run `npm run check` separately before installing your plugin: the packer transpiles TypeScript but does not replace type checking. Repack and reinstall after code changes. There is currently no hot reload or watch command.

Next: [cheat sheet](cheat-sheet.md), [lifecycle](lifecycle.md), [API reference](api.md).
