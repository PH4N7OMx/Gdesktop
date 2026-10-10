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

Соседние репозитории codegen и lib_ui синхронизированы с используемыми submodules. Languages обновлён из GummyGram upstream/main с сохранением дополнений форка. lib_tl и lib_icu проверены: обновлений не требуется.

Обновления отправлены в PH4N7OMx/codegen (master-codegen), PH4N7OMx/lib_ui (master-ui) и PH4N7OMx/Languages (main) с явным --force-with-lease; удалённые ревизии проверены после push. Интеграция включается в основной репозиторий Gdesktop в ветке dev вместе с актуальными указателями submodules.

## Проверка

Проверены все перенесённые файлы, уникальность языковых ключей и активные ссылки на них, Python AST, YAML workflow, пути CMake и идемпотентность патча codegen. git diff --check проходит. Дополнительно прошли все 15 существующих тестов SDK плагинов. Оригиналы затронутых файлов сохранены локально в .git/gummy-upgrade-7.3.0/before; дополнительная копия исходного патча codegen остаётся в stash submodule и уже включена в опубликованный commit.

Локальная сборка C++ и запуск клиента не выполнялись: AGENTS.md предписывает избегать сборки проекта. Первые запуски GitHub Actions выявили ошибки интеграции, поэтому перенос пока нельзя считать подтверждённым успешной сборкой.

## Дополнительная проверка совместимости

Run 37944123203 прошёл подготовку Qt на Windows и дошёл до компиляции клиента. Windows и Linux выявили ошибки в history_item.cpp: const-метод isTtlCoveredMedia вызывал isMessageSavable с const HistoryItem*, а новый upstream applySentMessage использовал отсутствующий MessageFlag::NoForwards. Проверка сохранения теперь принимает not_null<const HistoryItem*> (она не изменяет сообщение). Обновление флага отправленного сообщения использует JelNoForwards и is_jelNoforwards: обычный is_noforwards намеренно возвращает false в генераторе форка. Проверены все прямые обращения к MessageFlag во всех исходниках; отсутствующих значений нет. В завершённых Windows/Linux логах других ошибок компилятора не найдено; успешная сборка исправления ещё не подтверждена.

После ошибок CI проверены изменения заголовков между v7.2.9 и v7.3.0 и их обращения из кода форка. Исправлены получение внешнего wallet-engine.patch, сериализация MsgId, вызов построителя настроек плагинов и новый обязательный ElementDelegate::elementGramReadLine. Для истории изменений сообщений последний возвращает nullptr, как в upstream admin log; отображение Gram использует предусмотренный upstream вариант без общей очереди анимации.

Поиск по удалённым и изменённым API дополнительно обнаружил IconCurrencyColored в окне доната. Он заменён на IconCurrencyTwoTone, актуальный вариант Telegram для цветной иконки TON. Проверены объявления и определения методов делегата истории, локальные include в 155 файлах форка и 358 используемых ключей перевода. Проверка схемы API не выявила в коде jel обращений к удалённому FirebasePNV или изменённым структурам bot verification. Проверены FullDate и отдельные биты JelDeleted/JelBurnt.

CI Linux, macOS и Windows теперь передаёт Ninja -k 0, чтобы собирать независимые ошибки компиляции за один запуск. Ошибки по-прежнему завершают шаг с ненулевым статусом и блокируют упаковку. Для Linux используется уже существующий KEEP_GOING в build.sh. Это проверка исходников и конфигурации; она не заменяет успешную компиляцию и проверку клиента на всех платформах.

Windows CI run 37928251151 остановился в Prepare Qt: qmlprofiler/qmlpreview требовали отсутствующую libEGL.lib. Последующее удаление qtdeclarative было ошибкой: хотя интерфейс клиента не использует QML, движок плагинов использует QJSEngine, и Telegram/CMakeLists.txt требует Qt::Qml. Это привело к отсутствующему Qt5QmlConfig.cmake в run 37936491206. Qtdeclarative восстановлен в рецептах Qt 5/6; Linux Dockerfile и Homebrew CI уже включают его.

Причина libEGL.lib находится в Qt 5 mkspecs/features/win32/opengl.prf: встроенные библиотеки ANGLE добавлялись независимо от qtConfig(angle), при внешнем tg_angle и -no-angle. Локальный qt5-static-angle.patch ограничивает это добавление конфигурацией встроенного ANGLE. Run 37940235978 выявил вторую часть проблемы: Qt использует общий QMAKE_LIBS_OPENGL_ES2 и QMAKE_LIBS_EGL, а рецепт задаёт библиотеки для Debug и Release. Инструменты QML поэтому не получали внешний tg_angle и падали с 163 неразрешёнными символами GL/EGL. Патч теперь выбирает соответствующий непустой список Debug/Release в opengl.prf и egl.prf, сохраняя общий список как fallback. Изменение EGL ограничено Windows с внешним ANGLE. git apply --check и применение на обоих исходниках точной версии Qt 5.15.19 проходят. Хэш локального патча включён в ключ стадии Qt, ошибки его применения останавливают подготовку, окончания строк патча зафиксированы в LF. Проверки исходников не подтверждают успешную линковку: она проверяется в CI.

Дополнительно новые команды Debug для libheif защищены debug/enddebug, чтобы skip-debug выполнял только Release. AST-тест обновлён для новых общих переменных рецептов. Прошли 6 тестов подготовки (включая наличие Qt Qml во всех рецептах и инвалидацию кэша патча), 3 теста patch_codegen, 15 тестов SDK плагинов и TypeScript typecheck. Проверены 293 пути QRC, 155 путей исходников форка в CMake, 41 рекурсивный submodule, локальные include, ключи переводов и объявления/определения ElementDelegate. Методы QJSEngine interruption и конструктор QJsonValue(qint64) сверены с заголовками Qt 5.15.19. Это не подтверждает компиляцию C++ или работу клиента: окончательная проверка зависит от завершения CI на всех трёх платформах.
