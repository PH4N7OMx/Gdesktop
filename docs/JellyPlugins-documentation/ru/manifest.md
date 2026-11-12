# Манифест и пакет

```json
{
  "id": "jelly.hello",
  "name": "Hello JellyPlugins v2",
  "version": "2.0.0",
  "apiVersion": 2,
  "capabilities": []
}
```

Обязательны `id`, `name`, `version`, `apiVersion: 2`, `capabilities`. Необязательны строки `author`, `description`; неизвестные поля отклоняются. ID: `[a-z][a-z0-9.-]{2,63}`, версия: три целых числа через точки, имя до 80 символов.

Категории `telegram.send`, `telegram.read`, `schedule`, `storage` описывают намерения и не выдают права. Метаданные автора не удостоверяют его подлинность.

`.jelly` — JSON с `formatVersion: 2`, `manifest` и одним ESM-бандлом в `code`, максимум 1 МиБ. Внешние импорты, bytecode, архивы, ресурсы и top-level await не поддерживаются. SHA-256 определяет ревизию содержимого, а не подпись автора.
