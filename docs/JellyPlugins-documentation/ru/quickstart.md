# Первый плагин

## Подготовка

Получи SDK: папка `jellyplugins` из исходного репозитория или архив `JellyPlugins-sdk.zip`. Установи поддерживаемую версию Node.js с npm и открой терминал в папке SDK.

```powershell
npm ci --ignore-scripts
npm run pack:hello
```

Пакет появится в `examples/hello/dist/jelly.hello.jelly`. Этот пример добавляет кнопку, увеличивает локальный счётчик и пишет результат в журнал.


## Проверить результат

Включи установленный пример, разреши `ui` и `storage`. На странице JellyPlugins появится кнопка «Проверить плагин». Нажми её, затем открой «Журнал действий»: ожидается `Кнопка нажата 1 раз`. Следующее нажатие увеличивает счётчик; значение хранится между запусками.

Если результат не появился, открой [отладку](debugging.md).

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

## Структура проекта

```text
jellyplugins/
├── sdk/index.d.ts          типы API
├── scripts/pack.mjs        упаковщик
└── examples/
    ├── sdk-types.ts        импорт типов для примеров
    └── my-plugin/
        ├── manifest.json  имя, версия и права
        ├── index.ts       исходники
        └── dist/*.jelly    готовый пакет
```

## Команды SDK

| Команда | Результат |
|---|---|
| `npm run check` | Проверка типов, без создания пакета |
| `node scripts/pack.mjs examples/my-plugin` | Упаковка своего плагина |
| `npm run pack:hello` | Пример с кнопкой |
| `npm run pack:feed` | Пример RSS → Telegram |
| `npm run pack:giveaways` | Пример наблюдения за сообщениями |
| `npm test` | Проверки SDK и примеров |

Перед установкой своего плагина отдельно выполни `npm run check`: упаковщик преобразует TypeScript, но не заменяет проверку типов. После изменения кода нужно заново упаковать и установить пакет. Автоматической перезагрузки и команды `watch` пока нет.

Дальше: [шпаргалка](cheat-sheet.md), [жизненный цикл](lifecycle.md), [API](api.md).
