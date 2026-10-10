// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "jel/ui/settings/settings_appearance.h"

#include "lang_auto.h"
#include "jel/jel_settings.h"
#include "jel/ui/boxes/font_selector.h"
#include "jel/ui/components/avatar_corners_preview.h"
#include "jel/ui/components/icon_picker.h"
#include "jel/ui/settings/jel_builder.h"
#include "jel/ui/settings/settings_jel_utils.h"
#include "jel/ui/settings/settings_main.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_jel_icons.h"
#include "styles/style_jel_styles.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace JelBuilder;

namespace {

bool HasDrawerBots(not_null<Window::SessionController*> controller) {
	// todo: maybe iterate through all accounts
	const auto bots = &controller->session().attachWebView();
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inMainMenu || !bot.media) {
			continue;
		}
		return true;
	}
	return false;
}

void BuildAppIcon(SectionBuilder &builder, JelSectionBuilder &jel) {
	builder.addSubsectionTitle({
		.id = u"jel/appIcon"_q,
		.title = tr::jel_AppIconHeader(),
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(ctx.container),
			.margin = st::settingsButtonNoIcon.padding,
		};
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	builder.addDivider();
	builder.addSkip();
	jel.addSettingToggle({
		.id = u"jel/hideNotificationBadge"_q,
		.title = tr::jel_HideNotificationBadge(),
		.getter = &JelSettings::hideNotificationBadge,
		.setter = &JelSettings::setHideNotificationBadge,
	});
	builder.addSkip();
	builder.addDividerText(tr::jel_HideNotificationBadgeDescription());
	builder.addSkip();
#else
    builder.addDivider();
    builder.addSkip();
#endif
}

void BuildAvatarCorners(SectionBuilder &builder, JelSectionBuilder &jel) {
	auto *settings = &JelSettings::getInstance();
	const auto controller = builder.controller();

	const auto mapRadius = [](int val)
	{
		if (val == 0) {
			return tr::jel_AvatarCornersSquare(tr::now).toUpper();
		} else if (val == 23) {
			return tr::jel_AvatarCornersCircle(tr::now).toUpper();
		}
		return QString::number(val);
	};

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto container = ctx.container;
		auto title = object_ptr<Ui::FlatLabel>(
			container,
			tr::jel_AvatarCorners(),
			st::defaultSubsectionTitle);
		const auto titleRaw = title.data();

		const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				settings->avatarCornersValue() | rpl::map(mapRadius),
				st::settingsPremiumNewBadge),
			st::jelBetaBadgePadding);
		badge->show();
		badge->setAttribute(Qt::WA_TransparentForMouseEvents);
		badge->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(badge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			const auto r = st::jelBetaBadgePadding.left();
			p.drawRoundedRect(badge->rect(), r, r);
		}, badge->lifetime());

		titleRaw->geometryValue() | rpl::on_next([=](QRect geometry) {
			badge->moveToLeft(
				geometry.x()
					+ titleRaw->textMaxWidth()
					+ st::settingsPremiumNewBadgePosition.x(),
				geometry.y()
					+ (geometry.height() - badge->height()) / 2);
		}, badge->lifetime());

		return {
			.widget = std::move(title),
			.margin = st::defaultSubsectionTitlePadding,
		};
	}, [] {
		return SearchEntry{
			.id = u"jel/avatarCorners"_q,
			.title = tr::jel_AvatarCorners(tr::now),
		};
	});

	auto *previewRaw = static_cast<AvatarCornersPreview*>(nullptr);
	builder.add([&](const Builder::WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<AvatarCornersPreview>(
			ctx.container,
			controller);
		previewRaw = preview.data();
		const auto vMargin = st::settingsButtonNoIcon.padding
			- st::defaultDialogRow.padding;
		return {
			.widget = std::move(preview),
			.margin = QMargins(0, vMargin.top(), 0, vMargin.bottom()),
		};
	});

	jel.addSlider({
		.id = u"jel/avatarCornersSlider"_q,
		.title = rpl::single(QString()),
		.showTitle = false,
		.steps = 24,
		.current = settings->avatarCorners(),
		.onChanged = [=](int val) {
			JelSettings::getInstance().setAvatarCorners(val);
			if (previewRaw) {
				previewRaw->update();
			}
		},
		.onFinalChanged = [=](int val) {
			JelSettings::getInstance().setAvatarCorners(val);
			ShowRestartPrompt(controller);
		},
	});

	jel.addSettingToggle({
		.id = u"jel/singleCornerRadius"_q,
		.title = tr::jel_SingleCornerRadius(),
		.getter = &JelSettings::singleCornerRadius,
		.setter = &JelSettings::setSingleCornerRadius,
	});

	builder.addSkip();
	builder.addDividerText(tr::jel_SingleCornerRadiusDescription());
	builder.addSkip();
}

void BuildAppearance(SectionBuilder &builder, JelSectionBuilder &jel) {
	auto *settings = &JelSettings::getInstance();

	builder.addSubsectionTitle(tr::jel_CategoryAppearance());

	jel.addSettingToggle({
		.id = u"jel/materialSwitches"_q,
		.altIds = { u"jel/newSwitchStyle"_q },
		.title = tr::jel_MaterialSwitches(),
		.getter = &JelSettings::materialSwitches,
		.setter = &JelSettings::setMaterialSwitches,
	});
	jel.addSettingToggle({
		.id = u"jel/disableCustomBackgrounds"_q,
		.altIds = { u"jel/customThemes"_q },
		.title = tr::jel_DisableCustomBackgrounds(),
		.getter = &JelSettings::disableCustomBackgrounds,
		.setter = &JelSettings::setDisableCustomBackgrounds,
	});
	jel.addSettingToggle({
		.id = u"jel/hidePremiumStatuses"_q,
		.title = tr::jel_HidePremiumStatuses(),
		.getter = &JelSettings::hidePremiumStatuses,
		.setter = &JelSettings::setHidePremiumStatuses,
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"jel/monoFont"_q,
		.title = tr::jel_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			settings->monoFont().isEmpty()
				? tr::jel_FontDefault(tr::now)
				: settings->monoFont()),
		.onClick = [=] {
			JelUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					JelSettings::getInstance().setMonoFont(font);
				});
		},
	});

	jel.addSectionDivider();
}

void BuildChatFolders(SectionBuilder &builder, JelSectionBuilder &jel) {
	builder.addSubsectionTitle(tr::jel_ChatFoldersHeader());

	jel.addSettingToggle({
		.id = u"jel/hideNotificationCounters"_q,
		.altIds = { u"jel/tabCounter"_q },
		.title = tr::jel_HideNotificationCounters(),
		.getter = &JelSettings::hideNotificationCounters,
		.setter = &JelSettings::setHideNotificationCounters,
	});
	jel.addSettingToggle({
		.id = u"jel/hideAllChatsFolder"_q,
		.altIds = { u"jel/hideAllChats"_q },
		.title = tr::jel_HideAllChats(),
		.getter = &JelSettings::hideAllChatsFolder,
		.setter = &JelSettings::setHideAllChatsFolder,
	});

	jel.addSectionDivider();
}

void BuildTrayElements(SectionBuilder &builder, JelSectionBuilder &jel) {
	builder.addSubsectionTitle(tr::jel_TrayElementsHeader());

	jel.addSettingToggle({
		.id = u"jel/showGhostToggleInTray"_q,
		.title = tr::jel_EnableGhostModeTray(),
		.getter = &JelSettings::showGhostToggleInTray,
		.setter = &JelSettings::setShowGhostToggleInTray,
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	jel.addSettingToggle({
		.id = u"jel/showStreamerToggleInTray"_q,
		.title = tr::jel_EnableStreamerModeTray(),
		.getter = &JelSettings::showStreamerToggleInTray,
		.setter = &JelSettings::setShowStreamerToggleInTray,
	});
#endif

	jel.addSectionDivider();
}

void BuildDrawerElements(SectionBuilder &builder, JelSectionBuilder &jel) {
	builder.addSubsectionTitle(tr::jel_DrawerElementsHeader());

	jel.addSettingToggle({
		.id = u"jel/showMyProfileInDrawer"_q,
		.title = tr::lng_menu_my_profile(),
		.getter = &JelSettings::showMyProfileInDrawer,
		.setter = &JelSettings::setShowMyProfileInDrawer,
		.icon = { &st::menuIconProfile },
	});

	const auto controller = builder.controller();
	if (controller && HasDrawerBots(controller)) {
		jel.addSettingToggle({
			.id = u"jel/showBotsInDrawer"_q,
			.title = tr::lng_filters_type_bots(),
			.getter = &JelSettings::showBotsInDrawer,
			.setter = &JelSettings::setShowBotsInDrawer,
			.icon = { &st::menuIconBot },
		});
	}

	jel.addSettingToggle({
		.id = u"jel/showNewGroupInDrawer"_q,
		.title = tr::lng_create_group_title(),
		.getter = &JelSettings::showNewGroupInDrawer,
		.setter = &JelSettings::setShowNewGroupInDrawer,
		.icon = { &st::menuIconGroups },
	});
	jel.addSettingToggle({
		.id = u"jel/showNewChannelInDrawer"_q,
		.title = tr::lng_create_channel_title(),
		.getter = &JelSettings::showNewChannelInDrawer,
		.setter = &JelSettings::setShowNewChannelInDrawer,
		.icon = { &st::menuIconChannel },
	});
	jel.addSettingToggle({
		.id = u"jel/showContactsInDrawer"_q,
		.title = tr::lng_menu_contacts(),
		.getter = &JelSettings::showContactsInDrawer,
		.setter = &JelSettings::setShowContactsInDrawer,
		.icon = { &st::menuIconUserShow },
	});
	jel.addSettingToggle({
		.id = u"jel/showCallsInDrawer"_q,
		.title = tr::lng_menu_calls(),
		.getter = &JelSettings::showCallsInDrawer,
		.setter = &JelSettings::setShowCallsInDrawer,
		.icon = { &st::menuIconPhone },
	});
	jel.addSettingToggle({
		.id = u"jel/showSavedMessagesInDrawer"_q,
		.title = tr::lng_saved_messages(),
		.getter = &JelSettings::showSavedMessagesInDrawer,
		.setter = &JelSettings::setShowSavedMessagesInDrawer,
		.icon = { &st::menuIconSavedMessages },
	});
	jel.addSettingToggle({
		.id = u"jel/showLReadToggleInDrawer"_q,
		.title = tr::jel_LReadMessages(),
		.getter = &JelSettings::showLReadToggleInDrawer,
		.setter = &JelSettings::setShowLReadToggleInDrawer,
		.icon = { &st::jelLReadMenuIcon },
	});
	jel.addSettingToggle({
		.id = u"jel/showSReadToggleInDrawer"_q,
		.title = tr::jel_SReadMessages(),
		.getter = &JelSettings::showSReadToggleInDrawer,
		.setter = &JelSettings::setShowSReadToggleInDrawer,
		.icon = { &st::jelSReadMenuIcon },
	});
	jel.addSettingToggle({
		.id = u"jel/showNightModeToggleInDrawer"_q,
		.title = tr::lng_menu_night_mode(),
		.getter = &JelSettings::showNightModeToggleInDrawer,
		.setter = &JelSettings::setShowNightModeToggleInDrawer,
		.icon = { &st::menuIconNightMode },
	});
	jel.addSettingToggle({
		.id = u"jel/showGhostToggleInDrawer"_q,
		.title = tr::jel_GhostModeToggle(),
		.getter = &JelSettings::showGhostToggleInDrawer,
		.setter = &JelSettings::setShowGhostToggleInDrawer,
		.icon = { &st::jelGhostIcon },
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	jel.addSettingToggle({
		.id = u"jel/showStreamerToggleInDrawer"_q,
		.title = tr::jel_StreamerModeToggle(),
		.getter = &JelSettings::showStreamerToggleInDrawer,
		.setter = &JelSettings::setShowStreamerToggleInDrawer,
		.icon = { &st::jelStreamerModeMenuIcon },
	});
#endif

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = JelAppearance::Id(),
	.parentId = JelMain::Id(),
	.title = &tr::jel_CategoryAppearance,
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	auto jel = JelSectionBuilder(builder);

	builder.addSkip();
	BuildAppIcon(builder, jel);
	BuildAvatarCorners(builder, jel);
	BuildAppearance(builder, jel);
	BuildChatFolders(builder, jel);
	BuildTrayElements(builder, jel);
	BuildDrawerElements(builder, jel);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> JelAppearance::title() {
	return tr::jel_CategoryAppearance();
}

JelAppearance::JelAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void JelAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type JelAppearanceId() {
	return JelAppearance::Id();
}

} // namespace Settings
