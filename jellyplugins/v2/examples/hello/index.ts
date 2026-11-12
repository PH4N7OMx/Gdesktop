import type { Plugin } from "../../sdk";

const activate: Plugin = api => {
  api.log.info("JellyPlugins v2 activated");
};

export default activate;
