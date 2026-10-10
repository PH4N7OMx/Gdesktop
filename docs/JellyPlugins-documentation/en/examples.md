# Automation examples

The SDK includes three independent examples using the public API.

## Hello

`examples/hello` adds an action button, stores a counter and writes to the log. Build with `npm run pack:hello`.

## Feed to chat

`examples/feed-to-chat` reads a simple UTF-8 RSS feed over HTTPS and sends new links to one approved chat. Replace the example host and chat ID in the manifest, then run `npm run pack:feed`.

Configure `url`, `chatId` and `dryRun: true`. Enable the plugin with website, send, storage, timer and UI permissions. The first run records current entries without sending them. Preview new entries in the log before setting `dryRun: false` and enabling again.

Each poll sends at most one new entry and backs off after errors. The example does not log into websites, bypass anti-bot controls or copy media from web pages. A crash between server acceptance and saving local state may produce duplicates; exactly-once delivery is not provided.

## Giveaway watch

`examples/giveaway-watch` detects giveaway keywords in incoming messages and stores the last 30 matches. Replace the channel ID in `readChats` and run `npm run pack:giveaways`. Its button lists matches in the log. This example only observes messages; it does not join channels or press bot buttons.

Use the additional Telegram methods to build your own workflows with explicit permissions, rate handling and deduplication. Treat website and chat text as data, rather than permission to expand access.
