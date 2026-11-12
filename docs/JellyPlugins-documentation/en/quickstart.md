# Your first plugin

Node.js/npm are development tools, not client runtime dependencies.

```sh
cd jellyplugins
npm ci
npm run check
npm test
npm run pack:hello
```

Install `v2/examples/hello/dist/jelly.hello.jelly` in plugin settings of a new build, then enable it. It only writes an activation line to the log.

`v2/examples/hello/index.ts`:

```ts
import type { Plugin } from "../../sdk";

const activate: Plugin = api => {
  api.log.info("JellyPlugins v2 activated");
};

export default activate;

```

Create a folder containing `manifest.json` and `index.ts`, and change its manifest ID/name/version. From the SDK directory run `node v2/pack.mjs path/folder path/output.jelly`; quote paths containing spaces. Export an activation function as default. External runtime imports and Node.js APIs are unavailable.
