# GummyGram Plugin SDK

API 1 для TypeScript/JavaScript-плагинов. Документация: `../docs/GummyGram-documentation`.

```powershell
npm ci --ignore-scripts
npm run check
npm test
npm run pack:hello
```

Установи `examples/hello/dist/jelly.hello.jellyplugin` через настройки GummyGram → Плагины. Исполнение в первой реализации требует Windows AppContainer и Qt Qml в сборке клиента.

Другие примеры: `npm run pack:feed`, `npm run pack:giveaways`. Перед использованием замени примерные ID и домены в манифестах и изучи ограничения в документации.

Проверки SDK не подтверждают сборку, работу клиента или безопасность AppContainer. Не добавляй Node.js API или нативные зависимости в код плагина.
