import type { Plugin, PluginError } from "../sdk-types";

const activate: Plugin<{ url: string; chatId: string; dryRun: boolean }> = async (jelly, settings) => {
  if (!settings.url || !settings.chatId) throw new Error("Укажи url и chatId в настройках плагина");
  let busy = false;
  let retryAt = 0;
  const plain = (text: string) => text.replace(/<!\[CDATA\[([\s\S]*?)\]\]>/g, "$1")
    .replace(/<[^>]*>/g, "").replace(/&amp;/g, "&").replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">").trim();
  const field = (item: string, name: string) => plain(
    item.match(new RegExp(`<${name}(?:\\s[^>]*)?>([\\s\\S]*?)</${name}>`, "i"))?.[1] ?? "");
  const poll = async () => {
    if (busy || Date.now() < retryAt) return;
    busy = true;
    try {
      const response = await jelly.http.get(settings.url);
      const matches: string[] = [];
      const pattern = /<item(?:\s[^>]*)?>([\s\S]*?)<\/item>/gi;
      let match: RegExpExecArray | null;
      while ((match = pattern.exec(response.text)) && matches.length < 100) matches.push(match[1]);
      const items = matches
        .map(item => ({ title: field(item, "title"), link: field(item, "link"),
          id: field(item, "guid") || field(item, "link") }))
        .filter(item => item.id && item.link.startsWith("https://")).slice(0, 100);
      const previous = await jelly.storage.get<string[]>("seen");
      if (!previous) {
        await jelly.storage.set("seen", items.map(item => item.id));
        jelly.log("Начальное состояние сохранено; старые записи не отправлены");
        return;
      }
      const next = items.find(item => !previous.includes(item.id));
      if (!next) return;
      const text = `${next.title}\n${next.link}`.slice(0, 4096);
      if (settings.dryRun !== false) {
        jelly.log(`Предпросмотр: ${text}`);
        return;
      }
      const result = await jelly.telegram.sendMessage(settings.chatId, text, { silent: true });
      await jelly.storage.set("seen", [...previous, next.id].slice(-500));
      jelly.log(`Отправлено сообщение ${result.messageId}`);
    } catch (cause) {
      const error = cause as PluginError;
      retryAt = Date.now() + Math.max(error.retryAfter || 60, 60) * 1000;
      jelly.log(error.message);
    } finally { busy = false; }
  };
  await jelly.ui.addAction("poll", "Проверить ленту сейчас");
  jelly.on("action", ({ id }) => id === "poll" ? poll() : undefined);
  jelly.on("timer", ({ name }) => name === "poll" ? poll() : undefined);
  await jelly.timers.every("poll", 60);
};

export default activate;
