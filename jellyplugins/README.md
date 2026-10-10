# JellyPlugins SDK

API 1 для TypeScript/JavaScript-плагинов. [Документация](https://jellygram.gitbook.io/jelly-plugins/).

```powershell
npm ci --ignore-scripts
npm run check
npm test
npm run pack:hello
```

Установи `examples/hello/dist/jelly.hello.jelly` через настройки GummyGram → JellyPlugins. Исполнение в первой реализации требует Windows AppContainer и Qt Qml в сборке клиента.

Другие примеры: `npm run pack:feed`, `npm run pack:giveaways`. Перед использованием замени примерные ID и домены в манифестах и изучи ограничения в документации.

Проверки SDK не подтверждают сборку, работу клиента или безопасность AppContainer. Не добавляй Node.js API или нативные зависимости в код плагина.

Пример меню сообщений и формы настроек: `npm run pack:tools`. Перед упаковкой укажи свой ID чата в `examples/message-tools/manifest.json`.
