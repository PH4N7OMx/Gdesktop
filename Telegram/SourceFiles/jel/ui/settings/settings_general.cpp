// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "jel/ui/settings/settings_general.h"

#include "lang_auto.h"
#include "jel/jel_settings.h"
#include "jel/ui/settings/jel_builder.h"
#include "jel/ui/settings/settings_jel_utils.h"
#include "jel/ui/settings/settings_main.h"
#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "lang/lang_text_entity.h"
#include "platform/platform_translate_provider.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace JelBuilder;

namespace {

void BuildTranslator(SectionBuilder &builder, JelSectionBuilder &jel) {
	builder.addSubsectionTitle(tr::lng_translate_settings_subtitle());

	auto *settings = &JelSettings::getInstance();

	const auto options = std::vector{
		std::pair(TranslationProvider::Telegram, QString("Telegram")),
		std::pair(TranslationProvider::Google, QString("Google")),
		std::pair(TranslationProvider::Yandex, QString("Yandex")),
	};
	const auto nativeAvailable = Platform::IsTranslateProviderAvailable();
	auto availableOptions = options;
	if (nativeAvailable) {
		availableOptions.push_back(std::pair(
			TranslationProvider::Native,
			[] {
				if constexpr (Platform::IsMac()) {
					return QString("macOS");
				} else if constexpr (Platform::IsWindows()) {
					return QString("Windows");
				} else {
					return QString("Linux");
				}
			}()));
	}
	auto optionLabels = std::vector<QString>();
	optionLabels.reserve(availableOptions.size());
	for (const auto &option : availableOptions) {
		optionLabels.push_back(option.second);
	}

	const auto getIndex = [=](TranslationProvider val) {
		const auto i = ranges::find(
			availableOptions,
			val,
			&std::pair<TranslationProvider, QString>::first);
		return (i != end(availableOptions))
			? int(i - begin(availableOptions))
			: 0;
	};

	auto currentVal = JelSettings::getInstance().translationProviderValue()
		| rpl::map(getIndex)
		| rpl::map([=](int val) { return availableOptions[val].second; });

	const auto button = builder.addButton({
		.id = u"jel/translationProvider"_q,
		.title = tr::jel_TranslationProvider(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			if (const auto controller = Core::App().activeWindow()->sessionController()) {
				controller->show(Box(
						[=](not_null<Ui::GenericBox*> box) {
							const auto save = [=](int index) {
								const auto option = availableOptions[index].first;
								JelSettings::getInstance().setTranslationProvider(option);

								if constexpr (Platform::IsMac()) {
									if (option == TranslationProvider::Native) {
										controller->showToast(Ui::Toast::Config{
											.text = tr::lng_translate_settings_use_platform_mac_about(tr::now, tr::rich),
											.duration = 6 * crl::time(1000)
										});
									}
								}
							};
							SingleChoiceBox(box, {
								.title = tr::jel_TranslationProvider(),
								.options = optionLabels,
								.initialSelection = getIndex(settings->translationProvider()),
								.callback = save,
							});
						}));
			}
		},
	});
	if (button) {
		jel.addBetaBadge(button);
	}
}

void BuildShowPeerId(SectionBuilder &builder) {
	auto *settings = &JelSettings::getInstance();

	const auto options = std::vector{
		QString(tr::jel_SettingsShowID_Hide(tr::now)),
		QString("Telegram API"),
		QString("Bot API")
	};

	auto currentVal = JelSettings::getInstance().showPeerIdValue()
		| rpl::map([=](PeerIdDisplay val) {
			return options[static_cast<int>(val)];
		});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"jel/showPeerId"_q,
		.altIds = { u"jel/showIdAndDc"_q },
		.title = tr::jel_SettingsShowID(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						JelSettings::getInstance().setShowPeerId(
							static_cast<PeerIdDisplay>(index));
					};
					SingleChoiceBox(box, {
						.title = tr::jel_SettingsShowID(),
						.options = options,
						.initialSelection = static_cast<int>(settings->showPeerId()),
						.callback = save,
					});
				}));
		},
	});
}

void BuildQoLToggles(SectionBuilder &builder, JelSectionBuilder &jel) {
	auto *settings = &JelSettings::getInstance();

	BuildTranslator(builder, jel);
	jel.addSectionDivider();

	builder.addSubsectionTitle(tr::jel_CategoryGeneral());

	const auto controller = builder.controller();
	jel.addToggle({
		.id = u"jel/disableStories"_q,
		.altIds = { u"jel/hideStories"_q },
		.title = tr::jel_DisableStories(),
		.getter = [=] { return settings->disableStories(); },
		.setter = [=](bool enabled) {
			JelSettings::getInstance().setDisableStories(enabled);
			ShowRestartPrompt(controller);
		},
	});

	jel.addSettingToggle({
		.id = u"jel/disableOpenLinkWarning"_q,
		.title = tr::jel_DisableOpenLinkWarning(),
		.getter = &JelSettings::disableOpenLinkWarning,
		.setter = &JelSettings::setDisableOpenLinkWarning,
	});

	jel.addCollapsibleToggle({
		.id = u"jel/similarChannels"_q,
		.title = tr::jel_DisableSimilarChannels(),
		.checkboxes = {
			NestedEntry{
				tr::jel_CollapseSimilarChannels(),
				[] { return JelSettings::getInstance().collapseSimilarChannels(); },
				[](bool v) { JelSettings::getInstance().setCollapseSimilarChannels(v); }
			},
			NestedEntry{
				tr::jel_HideSimilarChannelsTab(),
				[] { return JelSettings::getInstance().hideSimilarChannels(); },
				[](bool v) { JelSettings::getInstance().setHideSimilarChannels(v); }
			}
		},
		.toggledWhenAll = true,
	});

	jel.addSettingToggle({
		.id = u"jel/disableNotificationsDelay"_q,
		.title = tr::jel_DisableNotificationsDelay(),
		.getter = &JelSettings::disableNotificationsDelay,
		.setter = &JelSettings::setDisableNotificationsDelay,
	});

	jel.addSettingToggle({
		.id = u"jel/loadBlockedAvatars"_q,
		.title = tr::jel_LoadBlockedAvatars(),
		.getter = &JelSettings::loadBlockedAvatars,
		.setter = &JelSettings::setLoadBlockedAvatars,
	});
	builder.addDividerText(tr::jel_LoadBlockedAvatarsDescription());

	jel.addSectionDivider();

	const auto zalgoButton = builder.addButton({
		.id = u"jel/filterZalgo"_q,
		.title = tr::jel_FilterZalgo(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filterZalgo()),
	});
	if (zalgoButton) {
		zalgoButton->toggledValue(
		) | rpl::filter(
			[=](bool enabled) {
				return (enabled != settings->filterZalgo());
			}
		) | on_next(
			[=](bool enabled) {
				JelSettings::getInstance().setFilterZalgo(enabled);
				ShowRestartPrompt(controller);
			},
			zalgoButton->lifetime());
		jel.addBetaBadge(zalgoButton);
	}

	jel.addSettingToggle({
		.id = u"jel/improveLinkPreviews"_q,
		.title = tr::jel_ImproveLinkPreviews(),
		.getter = &JelSettings::improveLinkPreviews,
		.setter = &JelSettings::setImproveLinkPreviews,
	});
	jel.addCollapsibleToggle({
		.id = u"jel/confirmations"_q,
		.title = tr::jel_ConfirmationsTitle(),
		.checkboxes = {
			NestedEntry{
				tr::jel_StickerConfirmation(tr::now),
				[] { return JelSettings::getInstance().stickerConfirmation(); },
				[](bool v) { JelSettings::getInstance().setStickerConfirmation(v); }
			},
			NestedEntry{
				tr::jel_GIFConfirmation(tr::now),
				[] { return JelSettings::getInstance().gifConfirmation(); },
				[](bool v) { JelSettings::getInstance().setGifConfirmation(v); }
			},
			NestedEntry{
				tr::jel_VoiceConfirmation(tr::now),
				[] { return JelSettings::getInstance().voiceConfirmation(); },
				[](bool v) { JelSettings::getInstance().setVoiceConfirmation(v); }
			},
			NestedEntry{
				tr::jel_RoundConfirmation(tr::now),
				[] { return JelSettings::getInstance().roundConfirmation(); },
				[](bool v) { JelSettings::getInstance().setRoundConfirmation(v); }
			}
		},
		.toggledWhenAll = false,
	});
	jel.addSettingToggle({
		.id = u"jel/showMessageSeconds"_q,
		.altIds = { u"jel/formatTimeWithSeconds"_q },
		.title = tr::jel_SettingsShowMessageSeconds(),
		.getter = &JelSettings::showMessageSeconds,
		.setter = &JelSettings::setShowMessageSeconds,
	});

	BuildShowPeerId(builder);

	jel.addSectionDivider();

	builder.addSubsectionTitle(rpl::single(QString("Webview")));

	jel.addSettingToggle({
		.id = u"jel/spoofWebviewAsAndroid"_q,
		.title = tr::jel_SettingsSpoofWebviewAsAndroid(),
		.getter = &JelSettings::spoofWebviewAsAndroid,
		.setter = &JelSettings::setSpoofWebviewAsAndroid,
	});

	jel.addCollapsibleToggle({
		.id = u"jel/biggerWindow"_q,
		.title = tr::jel_SettingsBiggerWindow(),
		.checkboxes = {
			NestedEntry{
				tr::jel_SettingsIncreaseWebviewHeight(),
				[] { return JelSettings::getInstance().increaseWebviewHeight(); },
				[](bool v) { JelSettings::getInstance().setIncreaseWebviewHeight(v); }
			},
			NestedEntry{
				tr::jel_SettingsIncreaseWebviewWidth(),
				[] { return JelSettings::getInstance().increaseWebviewWidth(); },
				[](bool v) { JelSettings::getInstance().setIncreaseWebviewWidth(v); }
			}
		},
		.toggledWhenAll = false,
	});
}

const auto kMeta = BuildHelper({
	.id = JelGeneral::Id(),
	.parentId = JelMain::Id(),
	.title = &tr::jel_CategoryGeneral,
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	auto jel = JelSectionBuilder(builder);

	builder.addSkip();
	BuildQoLToggles(builder, jel);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> JelGeneral::title() {
	return tr::jel_CategoryGeneral();
}

JelGeneral::JelGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void JelGeneral::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type JelGeneralId() {
	return JelGeneral::Id();
}

} // namespace Settings
