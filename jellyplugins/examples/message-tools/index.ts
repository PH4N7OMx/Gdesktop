import type { Plugin } from "../../sdk/index";

const activate: Plugin<{ enabled: boolean; prefix: string; maxChars: number; mode: string }> = async (jelly, settings) => {
  if (!settings.enabled) return;
  await jelly.ui.addMessageAction("inspect", "Inspect message");
  jelly.on("message.action", async ({ id, message }) => {
    if (id !== "inspect") return;
    const text = settings.prefix + ": " + message.text.slice(0, Math.floor(settings.maxChars));
    if (settings.mode === "Log") {
      jelly.log(text);
    } else {
      const confirmed = await jelly.ui.confirm("Selected message", text || "Empty message");
      if (confirmed) await jelly.ui.showToast("Message inspected");
    }
  });
  await jelly.ui.addAction("status", "Show status");
  jelly.on("action", async ({ id }) => {
    if (id === "status") await jelly.ui.showToast("Message Tools is running");
  });
};
export default activate;
