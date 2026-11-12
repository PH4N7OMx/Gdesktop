# JellyPlugins: maintainer notes

Новая реализация API 2 подключена к исходникам клиента; native и визуальные испытания ещё предстоят. [План и статус](jellyplugins-v2-plan.md), [схема и команды SDK](../../jellyplugins/v2/README.md), [RU/EN API](../JellyPlugins-documentation/README.md).

## Границы компонентов

| Компонент | Назначение |
|---|---|
| `jellyplugins/v2/contract.json` | Методы, нормализация, бюджеты и генерация SDK/bootstrap/native-каталога |
| `v2/host` | Отдельный Qt-free QuickJS executable, IPC и обязательная изоляция |
| `v2/client` | Launcher, Supervisor, проверка реальных действий, политика, Scheduler и Store |
| `plugin_service` | Экземпляры по аккаунтам, подтверждения, состояние, лимиты и обновления |
| `plugin_telegram` | Telegram bridge после решения политики, без автоматических повторов |
| `settings_plugins` | Управление и подтверждение фактических операций |

Старые manager/package/worker/sandbox, QJSEngine bootstrap и SDK 1 удалены. Остаточные данные прежней установки не читаются. Нет совместимого режима или переноса разрешений.

## Подготовка выпуска

`release.yml` и `canary.yml` сначала вызывают `jellyplugins-v2.yml`: Windows x64, Linux x64, macOS arm64 и x64. QuickJS инициализируется из вложенного widget-sandbox submodule. SDK проверяется без установки npm в продукт. Qt SDK нужен только native client tests; host зависит от QuickJS и системных библиотек.

Host копируется CMake в `jelly-runtime` или `Contents/Helpers`; он включён в portable, updater и Inno Setup. `verify_jelly_runtime.py` проверяет наличие, executable bit и архитектуры перед упаковкой. macOS подписывается через `sign_jelly_host.py`: helper имеет только app-sandbox, корневая подпись обновляется после отдельной подписи helper. Нельзя заменить это обычным `codesign --deep` с широкими entitlements клиента.

Проверять без сборки:

```sh
python Telegram/build/validate_build_inputs.py
python -m unittest discover -s Telegram/build/tests
python -m unittest discover -s Telegram/build/prepare/tests
npm ci --prefix jellyplugins
npm run check --prefix jellyplugins
npm test --prefix jellyplugins
```

Репозиторий запрещает локальную сборку через AGENTS.md. Native tests и полный клиент проверяются в Actions. Успех текстовых/SDK-тестов не является свидетельством native-успеха.

## Ручные проверки

Проверить фактический текст/чат/аккаунт до отправки, чтение истории только после согласия, сочетания режимов, смену параметров и отзыв прав. Проверить неизвестный результат отправки без повторов, FloodWait, несколько аккаунтов, интервалы после сна, чистую установку, обновление/откат, аварийный запуск и удаление. Проверить установленный/portable/bundle helper под обычным пользователем, подпись macOS, длинные строки, темы, масштабы и клавиатуру.

Публичная документация в `docs/JellyPlugins-documentation` обновлена локально под API 2. Публикация на [GitBook](https://jellygram.gitbook.io/jelly-plugins/) выполняется отдельно; изменение файлов не обновляет сайт автоматически.
