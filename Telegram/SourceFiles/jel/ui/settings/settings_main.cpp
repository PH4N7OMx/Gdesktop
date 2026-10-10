// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "jel/ui/settings/settings_main.h"

#include "settings/sections/settings_main.h"
#include "lang_auto.h"
#include "jel/jel_settings.h"
#include "jel/ui/jel_logo.h"
#include "jel/ui/settings/settings_appearance.h"
#include "jel/ui/settings/settings_jel.h"
#include "jel/ui/settings/settings_chats.h"
#include "jel/ui/settings/settings_filters.h"
#include "jel/ui/settings/settings_general.h"
#include "jel/ui/settings/settings_other.h"
#include "jel/ui/settings/settings_plugins.h"
#include "core/version.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_jel_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include <QDesktopServices>

namespace Settings {

using namespace Builder;

namespace {

void BuildLogo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto logo = object_ptr<Ui::RpWidget>(ctx.container);
		const auto logoRaw = logo.data();
		logoRaw->resize(
			QSize(st::settingsCloudPasswordIconSize,
				st::settingsCloudPasswordIconSize));
		logoRaw->setNaturalWidth(st::settingsCloudPasswordIconSize);
		logoRaw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(logoRaw);
			const auto image = JelAssets::currentAppLogoPad();
			if (!image.isNull()) {
				const auto size = st::settingsCloudPasswordIconSize;
				const auto scaled = image.scaled(
					size * style::DevicePixelRatio(),
					size * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
				p.drawImage(QRect(0, 0, size, size), scaled);
			}
		}, logoRaw->lifetime());
		return { .widget = std::move(logo), .align = style::al_top };
	});
}

void BuildVersionInfo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(
					QString("GummyGram Desktop v")
					+ QString::fromLatin1(AppVersionStr)),
				st::boxTitle),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				tr::jel_SettingsDescription(),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});
}

void BuildCategories(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::jel_CategoriesHeader());

	builder.addSectionButton({
		.title = rpl::single(u"Gummy"_q),
		.targetSection = JelGhost::Id(),
		.icon = { &st::menuIconGroupReactions },
	});
	builder.addSectionButton({
		.title = tr::jel_CategoryFilters(),
		.targetSection = JelFilters::Id(),
		.icon = { &st::menuIconTagFilter },
	});
	builder.addSectionButton({
		.title = tr::jel_CategoryGeneral(),
		.targetSection = JelGeneral::Id(),
		.icon = { &st::menuIconShowAll },
	});
	builder.addSectionButton({
		.title = tr::jel_CategoryAppearance(),
		.targetSection = JelAppearance::Id(),
		.icon = { &st::menuIconPalette },
	});
	builder.addSectionButton({
		.title = tr::jel_CategoryChats(),
		.targetSection = JelChats::Id(),
		.icon = { &st::menuIconChatBubble },
	});
	builder.addSectionButton({
		.title = tr::jel_JellyPlugins(),
		.targetSection = JelPlugins::Id(),
		.icon = { &st::menuIconBot },
	});
}

void BuildLinks(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::jel_LinksHeader());

	const auto controller = builder.controller();

	builder.addButton({
		.id = u"jel/channel"_q,
		.title = tr::jel_LinksChannel(),
		.icon = { &st::menuIconChannel },
		.label = rpl::single(QString("@GummyDesktop")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("GummyDesktop"),
			});
		},
	});
	builder.addButton({
		.id = u"jel/chat"_q,
		.title = tr::jel_LinksChats(),
		.icon = { &st::menuIconChats },
		.label = rpl::single(u"@GdesktopChat"_q),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = u"GdesktopChat"_q,
			});
		},
	});
	builder.addButton({
		.id = u"jel/website"_q,
		.title = tr::jel_LinksDocumentation(),
		.icon = { &st::menuIconIpAddress },
		.label = rpl::single(QString("JellyPlugins documentation")),
		.onClick = [=] {
			QDesktopServices::openUrl(
				QString("https://jellygram.gitbook.io/jelly-plugins/"));
		},
	});

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = JelMain::Id(),
	.parentId = MainId(),
	.title = &tr::jel_JelPreferences,
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	BuildLogo(builder);
	builder.addSkip();
	BuildVersionInfo(builder);
	BuildCategories(builder);
	BuildLinks(builder);
});

} // namespace

rpl::producer<QString> JelMain::title() {
	return rpl::single(QString(""));
}

JelMain::JelMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void JelMain::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type JelMainId() {
	return JelMain::Id();
}

} // namespace Settings
