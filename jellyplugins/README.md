# JellyPlugins SDK v2

Новая система подключена к исходникам GummyGram. Старые SDK, manager, worker и QJSEngine удалены; миграции API 1 нет. Установленный ранее клиент от изменения исходников не обновляется.

```powershell
npm ci
npm run check
npm test
npm run pack:hello
```

Результат: `v2/examples/hello/dist/jelly.hello.jelly`. Устанавливается через раздел плагинов в новой сборке. Нейтральный пример только пишет строку в журнал.

[Устройство runtime и сборка](v2/README.md) · [План и статус](../docs/maintainers/jellyplugins-v2-plan.md) · [Русская документация API](../docs/JellyPlugins-documentation/ru/README.md).

Платформенные проверки и сборки запускаются через GitHub Actions. Успех локальных JS/TypeScript-тестов не подтверждает исполнение native host или интерфейса.
