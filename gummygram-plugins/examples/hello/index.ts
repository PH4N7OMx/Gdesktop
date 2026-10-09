import type { Plugin } from "../sdk-types";

const activate: Plugin = async jelly => {
  await jelly.ui.addAction("hello", "Проверить плагин");
  jelly.on("action", async ({ id }) => {
    if (id !== "hello") return;
    const count = (await jelly.storage.get<number>("count") ?? 0) + 1;
    await jelly.storage.set("count", count);
    jelly.log(`Кнопка нажата ${count} раз`);
  });
};

export default activate;
