# Interface styles

The experimental macOS interface has been removed. The app uses the regular
Telegram Desktop layout, icons, fonts, palette, window controls and player.
The Telegram Desktop 7.4.2 update and existing Jel features are retained.

Appearance settings now offer Classic and Rounded switch styles. The setting
is stored as `roundedSwitches`, applies immediately and has two independent,
interactive preview switches. Legacy `macosInterface` and `interfaceIcons`
preferences are ignored.

The rounded switch geometry and spring response were adapted from Telegram
for macOS, TGUIKit/SwitchView.swift, revision 561169337e838c3281f7cfe24652757a5b522df9.
Source: https://github.com/TelegramMessenger/telegram-macos
The retained source remains under this repository's GPL license.

Validation: build-input validation, build/prepare Python tests and comparison
of C++ syntax parser diagnostics with the previous revision. No native build
or running-client visual verification was performed (see AGENTS.md).
