# Обновление базы GummyGram до Telegram Desktop 7.3.0

Дата: 9 октября 2026 года. Исходная база — `v7.2.9`, новая — [стабильный релиз v7.3.0](https://github.com/telegramdesktop/tdesktop/releases/tag/v7.3.0), commit `42f8a36d43b8c805bc821905bea4cfeb3af1d41d`.

Перенесены 868 изменённых файлов из приложения Telegram, его ресурсов, схемы API, CMake и подготовки зависимостей. Обновлены версии приложения и ресурсов Windows. Инструкции и автоматизация агентов upstream не импортировались.

Сохранены GummyGram/Gummy, канал @GummyDesktop, чат @GdesktopChat, отсутствие ссылки Crowdin, скрытая вкладка «Другое», плагины, Ghost Mode, сохранение сообщений и message-shot. 18 конфликтов согласованы вручную. Новый FullDate получил отдельный бит `0x10000`, тип флагов расширен до uint32; отметки удалённых и сгоревших сообщений сохранены. Согласованы новые ограничения media editor с исключениями message-shot. Updater перенесён на реализацию 7.3: она принимает только формат v2; адреса и ключи сервисов форка не заменены.

В macOS CI вместо отдельного tlottie собирается общий Rust-архив tlottie/wallet-engine с генерацией C++-привязок; кэш включает обе ревизии, toolchain и патч. Windows/Linux используют обновлённые рецепты upstream. Исправлена идемпотентность patch_codegen.py для обновлённого сканера, а устаревший кэш языковых подмножеств инвалидирован. Согласованы ссылки C++ на существующие ключи перевода.

## Библиотеки

| Путь | Ревизия |
| --- | --- |
| `Telegram/codegen` | `240c50bb495aa6a9ba388673eaa7d37ad5f1ca88` |
| `Telegram/lib_ui` | `2044fff4018876fc13c3b11eaa6f534372b05424` |
| `Telegram/lib_base` | `5462d363717621d9de767f09cd31dcaed09a45cf` |
| `Telegram/lib_rpl` | `1ae5ed73428e1f3dfa9160d5dccb1cf288444775` |
| `Telegram/lib_crl` | `de724667d7bdfbf906f78fc405af1ee9f7723d49` |
| `Telegram/lib_lottie` | `fbb922c30b7c30ba17ad5fd4fcb989eadd620062` |
| `Telegram/lib_translate` | `b23983df9dd87b6993afdcefb48daf890beb545e` |
| `Telegram/lib_webview` | `d6e2e0b8b171a104cd7b63bd351f056563e964b0` |
| `cmake` | `699262e441dbc6270f0ba6cc74f2e7a8e4c7e422` |
| `Telegram/ThirdParty/fcitx5-qt` | `0285a5d18367d8f3af80dab7e4819d5555982340` |
| `Telegram/ThirdParty/libprisma` | `75f26c17308871bee69f69a2831a8af2bc2f3627` |
| `Telegram/ThirdParty/tgcalls` | `1c236c09f8d8569fead14bd68000618a52051225` |
| `Telegram/ThirdParty/xxHash` | `c87183a77d67f7d37e3d2d1b7eaac5e7c695e4f0` |
| `Telegram/ThirdParty/zxcvbn` | `ab51000506afc1a450557a3608646bc04ba951fa` |

Соседние репозитории codegen и lib_ui синхронизированы с используемыми submodules. Languages обновлён из AyuGram upstream/main с сохранением дополнений форка. lib_tl и lib_icu проверены: обновлений не требуется.

Обновления отправлены в PH4N7OMx/codegen (master-codegen), PH4N7OMx/lib_ui (master-ui) и PH4N7OMx/Languages (main) с явным --force-with-lease; удалённые ревизии проверены после push. Интеграция включается в основной репозиторий Gdesktop в ветке dev вместе с актуальными указателями submodules.

## Проверка

Проверены все перенесённые файлы, уникальность языковых ключей и активные ссылки на них, Python AST, YAML workflow, пути CMake и идемпотентность патча codegen. git diff --check проходит. Дополнительно прошли все 15 существующих тестов SDK плагинов. Оригиналы затронутых файлов сохранены локально в .git/gummy-upgrade-7.3.0/before; дополнительная копия исходного патча codegen остаётся в stash submodule и уже включена в опубликованный commit.

Сборка C++, запуск клиента и новые CI-процессы не проверялись: AGENTS.md предписывает избегать сборки проекта.
