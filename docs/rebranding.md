# GummyGram branding and Jel internals

The client is **GummyGram Desktop**. The internal short name is **jel**: source and resource directories, includes, style names and translation prefixes use jel; C++ classes and namespaces use Jel. The plugin platform remains JellyPlugins and packages use .jelly.

The main repository pins the matching versions of codegen, lib_ui, lib_tl and lib_icu. Codegen recognizes lng_ and jel_ keys; subset cache version 3 invalidates older generated caches. Localization files and generators are maintained in the separate Languages repository without Crowdin.

Application identities use GummyGram on Windows/macOS and com.gummygram.desktop on Linux. The Telegram profile and JellyPlugins storage remain in their existing locations. Settings write tdata/jel_settings.json and can read legacy settings and backup files. A legacy message database is copied to tdata/jeldata.db using SQLite's online backup API, including committed WAL contents; the original database remains available as a backup. New databases use the Jel name. Legacy translation caches and settings links are accepted for compatibility.

Original author credits, licenses and upstream history links remain accurate. The existing update and remote-configuration endpoints and signing keys are retained until corresponding GummyGram infrastructure exists; they must not be replaced with invented URLs. Project links point to PH4N7OMx/Gdesktop, @GummyDesktop and @GdesktopChat.

Original sources for the two active logos are preserved in docs/assets/branding. Runtime icons are in Telegram/Resources/art/gummy; temporary archives and discarded design iterations are not source dependencies.

Native compilation and application startup are unverified: AGENTS.md says to avoid building the project. Focused codegen/prepare tests, SDK tests and source/resource checks are used instead.
