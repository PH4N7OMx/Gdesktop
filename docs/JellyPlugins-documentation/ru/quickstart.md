# Первый плагин

Node.js/npm нужны только разработчику. Пользователю готового клиента они не требуются.

```sh
cd jellyplugins
npm ci
npm run check
npm test
npm run pack:hello
```

Установи `v2/examples/hello/dist/jelly.hello.jelly` в настройках плагинов новой сборки и включи его. Пример только пишет строку в журнал.

`v2/examples/hello/index.ts`:

```ts
import type { Plugin } from "../../sdk";

const activate: Plugin = api => {
  api.log.info("JellyPlugins v2 activated");
};

export default activate;

```

Для своего пакета создай папку с `manifest.json` и `index.ts`, измени ID/имя/версию и вызови из каталога SDK `node v2/pack.mjs путь/папка путь/output.jelly`. Пути с пробелами заключай в кавычки. Код экспортирует функцию активации по умолчанию; внешние runtime-импорты и Node.js API недоступны.
