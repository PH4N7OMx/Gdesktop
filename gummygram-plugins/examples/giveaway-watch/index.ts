import type { Plugin, Json } from "../sdk-types";

const activate: Plugin = async jelly => {
  let queue = Promise.resolve();
  jelly.on("message.new", message => {
    if (!/розыгрыш|giveaway|конкурс/i.test(message.text)) return;
    queue = queue.catch(error => jelly.log(String(error))).then(async () => {
      const saved = await jelly.storage.get<Json[]>("giveaways") ?? [];
      const key = `${message.chatId}:${message.messageId}`;
      if (saved.some(item => typeof item === "object" && item !== null && !Array.isArray(item)
        && item.key === key)) return;
      await jelly.storage.set("giveaways", [...saved, {
        key, chatId: message.chatId, messageId: message.messageId,
        text: message.text.slice(0, 500), date: message.date,
      }].slice(-30));
      jelly.log(`Найден розыгрыш: ${key}`);
    });
    return queue;
  });
  await jelly.ui.addAction("list", "Показать найденные розыгрыши в журнале");
  jelly.on("action", async ({ id }) => {
    if (id !== "list") return;
    const saved = await jelly.storage.get<Json[]>("giveaways") ?? [];
    jelly.log(`Сохранено розыгрышей: ${saved.length}`);
    for (const item of saved.slice(-5)) jelly.log(JSON.stringify(item));
  });
};

export default activate;
