# Первый плагин

## Подготовка

Получи SDK: папка `jellyplugins` из исходного репозитория или архив `JellyPlugins-sdk.zip`. Установи поддерживаемую версию Node.js с npm и открой терминал в папке SDK.

```powershell
npm ci --ignore-scripts
npm run check
npm test
npm run pack:hello
```

Пакет появится в `examples/hello/dist/jelly.hello.jelly`. Этот пример добавляет кнопку, увеличивает локальный счётчик и пишет результат в журнал.

## Создать свой

Скопируй `examples/hello` в `examples/my-plugin`. Укажи новое имя и уникальный `id` в `manifest.json`. В `index.ts` экспортируй функцию активации по умолчанию.

```typescript
import type { Plugin } from "../sdk-types";

const activate: Plugin = async jelly => {
  await jelly.ui.addAction("hello", "Поздороваться");

  jelly.on("action", async ({ id }) => {
    if (id !== "hello") return;
    const count = (await jelly.storage.get<number>("count") ?? 0) + 1;
    await jelly.storage.set("count", count);
    jelly.log(`Привет! Запусков: ${count}`);
  });
};

export default activate;
```

```json
{
  "apiVersion": 1,
  "id": "my.first-plugin",
  "name": "Мой первый плагин",
  "author": "Имя автора",
  "version": "1.0.0",
  "permissions": {
    "ui": true,
    "storage": true
  }
}
```

```powershell
npm run check
node scripts/pack.mjs examples/my-plugin
```

Установи полученный `.jelly` через настройки клиента. Сам код TypeScript клиент не исполняет: упаковщик преобразует его в совместимый JavaScript.

## JavaScript

В `index.ts` можно использовать обычный JavaScript без аннотаций типов. Для библиотеки с несколькими файлами используй относительные импорты; упаковщик соберёт их в один кодовый пакет. Зависимости должны работать в чистом JavaScript: Node.js, DOM и нативные модули в клиенте отсутствуют.

## Отладка

Вызывай `jelly.log()` и открывай журнал в карточке плагина. Проверяй ошибки асинхронных действий через `try/catch`. Не отправляй сообщения из верхнего уровня модуля: регистрируй обработчики внутри функции активации.

Тесты SDK проверяют упаковку и контракт JavaScript. Работу клиента и изоляции нужно проверять отдельно после сборки.
