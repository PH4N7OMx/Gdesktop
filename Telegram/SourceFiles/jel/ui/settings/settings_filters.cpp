// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "jel/ui/settings/settings_filters.h"

#include "lang_auto.h"
#include "jel/jel_settings.h"
#include "jel/data/jel_database.h"
#include "jel/features/filters/filters_cache_controller.h"
#include "jel/ui/boxes/import_filters_box.h"
#include "jel/ui/settings/jel_builder.h"
#include "jel/ui/settings/settings_main.h"
#include "jel/utils/telegram_helpers.h"
#include "boxes/abstract_box.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "filters/per_dialog_filter.h"
#include "filters/settings_filters_list.h"
#include "inline_bots/bot_attach_web_view.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_jel_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace JelBuilder;

namespace {

void BuildGlobalFilters(SectionBuilder &builder) {
	auto *settings = &JelSettings::getInstance();

	builder.addSkip();
	builder.addSubsectionTitle(tr::jel_FiltersGlobalSection());

	const auto enabledButton = builder.addButton({
		.id = u"jel/filtersEnabled"_q,
		.title = tr::jel_RegexFiltersEnable(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabled()),
	});
	if (enabledButton) {
		enabledButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabled());
		}) | on_next([=](bool enabled) {
			JelSettings::getInstance().setFiltersEnabled(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, enabledButton->lifetime());
	}

	builder.addSubsectionTitle(tr::jel_FiltersScopeHeader());

	const auto channelsButton = builder.addButton({
		.id = u"jel/filtersEnabledInChannels"_q,
		.title = tr::jel_FiltersInChannels(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabledInChannels()),
	});
	if (channelsButton) {
		channelsButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabledInChannels());
		}) | on_next([=](bool enabled) {
			JelSettings::getInstance().setFiltersEnabledInChannels(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, channelsButton->lifetime());
	}

	const auto groupsButton = builder.addButton({
		.id = u"jel/filtersEnabledInGroups"_q,
		.altIds = { u"jel/filtersEnabledInChats"_q, u"jel/filtersInChats"_q },
		.title = tr::jel_FiltersInGroups(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabledInGroups()),
	});
	if (groupsButton) {
		groupsButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabledInGroups());
		}) | on_next([=](bool enabled) {
			JelSettings::getInstance().setFiltersEnabledInGroups(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, groupsButton->lifetime());
	}

	const auto privateButton = builder.addButton({
		.id = u"jel/filtersEnabledInPrivate"_q,
		.title = tr::jel_FiltersInPrivate(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabledInPrivate()),
	});
	if (privateButton) {
		privateButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabledInPrivate());
		}) | on_next([=](bool enabled) {
			JelSettings::getInstance().setFiltersEnabledInPrivate(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, privateButton->lifetime());
	}

	const auto blockedButton = builder.addButton({
		.id = u"jel/hideFromBlocked"_q,
		.title = tr::jel_FiltersHideFromBlocked(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->hideFromBlocked()),
	});
	if (blockedButton) {
		blockedButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->hideFromBlocked());
		}) | on_next([=](bool enabled) {
			JelSettings::getInstance().setHideFromBlocked(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, blockedButton->lifetime());
	}

	const auto collapseButton = builder.addButton({
		.id = u"jel/collapseDuplicates"_q,
		.title = tr::jel_CollapseDuplicates(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->collapseDuplicates()),
	});
	if (collapseButton) {
		collapseButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->collapseDuplicates());
		}) | on_next([=](bool enabled) {
			JelSettings::getInstance().setCollapseDuplicates(enabled);
		}, collapseButton->lifetime());
		JelSectionBuilder(builder).addBetaBadge(collapseButton);
	}

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"jel/sharedFilters"_q,
		.title = tr::jel_RegexFiltersShared(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->dialogId = std::nullopt;
			controller->showExclude = false;
			controller->showSettings(JelFiltersList::Id());
		},
	});

	builder.addButton({
		.id = u"jel/shadowBanIds"_q,
		.altIds = { u"jel/shadowBanList"_q },
		.title = tr::jel_FiltersShadowBan(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->dialogId = std::nullopt;
			controller->showExclude = false;
			controller->shadowBan = true;
			controller->showSettings(JelFiltersList::Id());
		},
	});

	builder.addSkip();
}

void BuildPerChatFilters(SectionBuilder &builder) {
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::jel_FiltersPerChatSection());

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"jel/selectChatFilters"_q,
		.title = tr::jel_FiltersAddPerChat(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			if (const auto window = Core::App().activeWindow()) {
				if (const auto scontroller = window->sessionController()) {
					auto types = InlineBots::PeerTypes();
					types |= InlineBots::PeerType::Bot;
					types |= InlineBots::PeerType::Group;
					types |= InlineBots::PeerType::Broadcast;

					Window::ShowChooseRecipientBox(
						scontroller,
						[=](not_null<Data::Thread*> thread) {
							const auto peer = thread->peer();
							controller->dialogId = getDialogIdFromPeer(peer);
							controller->showExclude = true;
							controller->showSettings(JelFiltersList::Id());
							return true;
						},
						tr::jel_FiltersMenuSelectChat(),
						nullptr,
						types);
				}
			}
		},
	});

	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			if (!JelDatabase::hasPerDialogFilters()) {
				return;
			}

			const auto container = wctx.container;
			const auto controller = wctx.controller;

			AddSkip(container);

			auto ctrl = container->lifetime().make_state<PerDialogFiltersListController>(
				&controller->session(),
				controller);

			auto list = object_ptr<Ui::PaddingWrap<PeerListContent>>(
				container,
				object_ptr<PeerListContent>(
					container,
					ctrl),
				QMargins(0, -st::peerListBox.padding.top(), 0, -st::peerListBox.padding.bottom()));
			AddSkip(container);
			const auto content = container->add(std::move(list));
			AddSkip(container);
			auto delegate = container->lifetime().make_state<PeerListContentDelegateSimple>();
			delegate->setContent(content->entity());
			ctrl->setDelegate(delegate);
		}, [&](const SearchContext &) {
		});
	});
}

const auto kMeta = BuildHelper({
	.id = JelFilters::Id(),
	.parentId = JelMain::Id(),
	.title = &tr::jel_CategoryFilters,
	.icon = &st::menuIconTagFilter,
}, [](SectionBuilder &builder) {
	BuildGlobalFilters(builder);
	BuildPerChatFilters(builder);
});

} // namespace

rpl::producer<QString> JelFilters::title() {
	return tr::jel_CategoryFilters();
}

void JelFilters::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	addAction(
		tr::jel_FiltersMenuSelectChat(tr::now),
		[=] {
			if (const auto window = Core::App().activeWindow()) {
				if (const auto controller = window->sessionController()) {
					auto types = InlineBots::PeerTypes();
					types |= InlineBots::PeerType::Bot;
					types |= InlineBots::PeerType::Group;
					types |= InlineBots::PeerType::Broadcast;

					Window::ShowChooseRecipientBox(
						controller,
						[=](not_null<Data::Thread*> thread) {
							const auto peer = thread->peer();
							controller->dialogId = getDialogIdFromPeer(peer);
							controller->showExclude = true;
							controller->showSettings(JelFiltersList::Id());
							return true;
						},
						tr::jel_FiltersMenuSelectChat(),
						nullptr,
						types);
				}
			}
		},
		&st::menuIconSearch);
	addAction({ .isSeparator = true });
	addAction(
		tr::jel_FiltersMenuImport(tr::now),
		[=] {
			auto box = Box(Ui::FillImportFiltersBox, true);
			Ui::show(std::move(box));
		},
		&st::menuIconArchive);
	if (JelDatabase::hasFilters()) {
		addAction(
			tr::jel_FiltersMenuExport(tr::now),
			[=] {
				auto box = Box(Ui::FillImportFiltersBox, false);
				Ui::show(std::move(box));
			},
			&st::menuIconUnarchive);
	}
	addAction({ .isSeparator = true });
	addAction({
		.text = tr::jel_FiltersMenuClear(tr::now),
		.handler = [=] {
			auto callback = [=](Fn<void()> &&close) {
				JelDatabase::deleteAllFilters();
				JelDatabase::deleteAllExclusions();
				FiltersCacheController::rebuildCache();
				FiltersCacheController::fireUpdate();
				close();
			};
			auto box = Ui::MakeConfirmBox({
				.text = tr::jel_FiltersClearPopupText(),
				.confirmed = callback,
				.confirmText = tr::jel_FiltersClearPopupActionText(),
				.confirmStyle = &st::attentionBoxButton,
			});
			Ui::show(std::move(box));
		},
		.icon = &st::menuIconClearAttention,
		.isAttention = true,
	});
}

JelFilters::JelFilters(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void JelFilters::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type JelFiltersId() {
	return JelFilters::Id();
}

} // namespace Settings
