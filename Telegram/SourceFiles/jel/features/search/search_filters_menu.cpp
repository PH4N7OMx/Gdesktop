#include "jel/features/search/search_filters_menu.h"

#include "data/data_peer.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/calendar_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_jel_icons.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Jel {
namespace {

[[nodiscard]] const style::icon *MediaFilterIcon(SearchMediaFilter filter) {
	switch (filter) {
	case SearchMediaFilter::All:
		return &st::menuIconShowAll;
	case SearchMediaFilter::Photos:
		return &st::menuIconPhoto;
	case SearchMediaFilter::Videos:
		return &st::menuIconVideoChat;
	case SearchMediaFilter::Files:
		return &st::menuIconFile;
	case SearchMediaFilter::Links:
		return &st::menuIconLinks;
	case SearchMediaFilter::Voice:
		return &st::messageFieldVoiceIcon;
	case SearchMediaFilter::Music:
		return &st::menuIconSoundSelect;
	case SearchMediaFilter::Gifs:
		return &st::menuIconGif;
	case SearchMediaFilter::Pinned:
		return &st::menuIconPin;
	case SearchMediaFilter::Polls:
		return &st::menuIconCreatePoll;
	}
	return &st::menuIconShowAll;
}

} // namespace

void ShowSearchDateRangeBox(
		not_null<Window::SessionController*> controller,
		SearchExtraFilters current,
		Fn<void(SearchExtraFilters)> onChanged) {
	const auto today = QDate::currentDate();
	const auto highlighted = [&] {
		if (current.datePreset == SearchDatePreset::CustomRange) {
			if (const auto to = UnpackSearchDate(current.customToDate); to.isValid()) {
				return std::min(to, today);
			}
			if (const auto from = UnpackSearchDate(current.customFromDate); from.isValid()) {
				return std::min(from, today);
			}
		}
		return today;
	}();

	struct ButtonState {
		enum class Type {
			None,
			Disabled,
			Active,
		};
		Type type = Type::None;
		style::complex_color disabledFg = style::complex_color([] {
			auto result = st::defaultBoxButton.textFg->c;
			result.setAlpha(result.alpha() / 2);
			return result;
		});
		style::RoundButton disabled = st::defaultBoxButton;
	};
	const auto buttonState = std::make_shared<ButtonState>();
	buttonState->disabled.textFg
		= buttonState->disabled.textFgOver
		= buttonState->disabledFg.color();
	buttonState->disabled.ripple.color
		= buttonState->disabled.textBgOver
		= buttonState->disabled.textBg;

	const auto applyRange = [=](QDate first, QDate last) {
		if (!first.isValid()) {
			return;
		}
		if (!last.isValid()) {
			last = first;
		}
		if (first > last) {
			std::swap(first, last);
		}
		auto updated = current;
		updated.datePreset = SearchDatePreset::CustomRange;
		updated.customFromDate = PackSearchDate(first);
		updated.customToDate = PackSearchDate(last);
		updated.normalizeDates();
		if (onChanged) {
			onChanged(updated);
		}
	};

	const auto selectionChanged = [=](
			not_null<Ui::CalendarBox*> box,
			std::optional<int> selected) {
		if (!selected.has_value()) {
			buttonState->type = ButtonState::Type::None;
			return;
		}
		const auto type = (*selected > 0)
			? ButtonState::Type::Active
			: ButtonState::Type::Disabled;
		if (buttonState->type == type) {
			return;
		}
		buttonState->type = type;
		box->clearButtons();
		box->addButton(tr::lng_cancel(), [=] {
			box->toggleSelectionMode(false);
		});
		const auto button = box->addLeftButton(
			tr::jel_SearchFilterApply(),
			[=] {
				const auto first = box->selectedFirstDate();
				const auto last = box->selectedLastDate();
				if (first.isValid()) {
					applyRange(first, last);
					box->closeBox();
				}
			},
			(*selected > 0) ? st::defaultBoxButton : buttonState->disabled);
		if (!*selected) {
			button->setPointerCursor(false);
		}
	};

	controller->show(Box<Ui::CalendarBox>(Ui::CalendarBoxArgs{
		.month = highlighted,
		.highlighted = highlighted,
		.callback = [=](const QDate &date, Fn<void()> close) {
			applyRange(date, date);
			if (close) {
				close();
			}
		},
		.minDate = QDate(2013, 8, 1),
		.maxDate = today,
		.allowsSelection = true,
		.selectionChanged = selectionChanged,
	}));
}

void ShowSearchFiltersMenu(
		base::unique_qptr<Ui::PopupMenu> &menu,
		QPoint globalPos,
		SearchFiltersMenuArgs args) {
	menu = base::make_unique_q<Ui::PopupMenu>(
		args.parent,
		st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(menu.get());
	const auto current = args.current;
	const auto inPeer = args.inPeer;
	const auto isPrivateUser = args.isPrivateUser;
	const auto onChanged = args.onChanged;
	const auto controller = args.controller;

	const auto apply = [=](Fn<void(SearchExtraFilters &)> mutate) {
		auto updated = current;
		mutate(updated);
		updated.sanitizeForContext(inPeer, isPrivateUser);
		if (onChanged) {
			onChanged(updated);
		}
	};

	if (inPeer) {
		const auto newest = (current.sortOrder == SearchSortOrder::NewestFirst);
		addAction({
			.text = tr::jel_SearchFilterSortNewest(tr::now),
			.handler = [=] {
				apply([](SearchExtraFilters &f) {
					f.sortOrder = SearchSortOrder::NewestFirst;
				});
			},
			.icon = newest ? &st::mediaPlayerMenuCheck : &st::menuIconCollapse,
		});
		addAction({
			.text = tr::jel_SearchFilterSortOldest(tr::now),
			.handler = [=] {
				apply([](SearchExtraFilters &f) {
					f.sortOrder = SearchSortOrder::OldestFirst;
				});
			},
			.icon = !newest ? &st::mediaPlayerMenuCheck : &st::menuIconExpand,
		});
		addAction({ .isSeparator = true });
	}

	if (!args.mediaFilterLocked) {
		const auto mediaText = (current.mediaFilter == SearchMediaFilter::All)
			? tr::jel_SearchFilterMedia(tr::now)
			: (tr::jel_SearchFilterMedia(tr::now)
				+ u": "_q
				+ SearchMediaFilterLabel(current.mediaFilter));
		addAction({
			.text = mediaText,
			.handler = nullptr,
			.icon = MediaFilterIcon(current.mediaFilter),
			.fillSubmenu = [=](not_null<Ui::PopupMenu*> submenu) {
				auto options = std::vector<SearchMediaFilter>{
					SearchMediaFilter::All,
					SearchMediaFilter::Photos,
					SearchMediaFilter::Videos,
					SearchMediaFilter::Files,
					SearchMediaFilter::Links,
					SearchMediaFilter::Voice,
					SearchMediaFilter::Music,
					SearchMediaFilter::Gifs,
				};
				if (inPeer) {
					options.push_back(SearchMediaFilter::Pinned);
					options.push_back(SearchMediaFilter::Polls);
				}
				for (const auto option : options) {
					const auto checked = (current.mediaFilter == option);
					submenu->addAction(
						SearchMediaFilterLabel(option),
						[=] {
							apply([=](SearchExtraFilters &f) {
								f.mediaFilter = option;
							});
						},
						checked ? &st::mediaPlayerMenuCheck : MediaFilterIcon(option));
				}
			},
			.submenuSt = &st::popupMenuWithIcons,
		});
	}

	const auto dateText = !current.hasDateFilter()
		? tr::jel_SearchFilterDate(tr::now)
		: (tr::jel_SearchFilterDate(tr::now)
			+ u": "_q
			+ SearchDateFilterLabel(current));
	addAction({
		.text = dateText,
		.handler = nullptr,
		.icon = &st::menuIconSchedule,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> submenu) {
			const auto presets = {
				SearchDatePreset::AnyTime,
				SearchDatePreset::Today,
				SearchDatePreset::Last7Days,
				SearchDatePreset::Last30Days,
				SearchDatePreset::LastYear,
			};
			for (const auto preset : presets) {
				const auto checked = (current.datePreset == preset);
				auto labelState = SearchExtraFilters{};
				labelState.datePreset = preset;
				submenu->addAction(
					SearchDateFilterLabel(labelState),
					[=] {
						apply([=](SearchExtraFilters &f) {
							f.datePreset = preset;
							f.customFromDate = 0;
							f.customToDate = 0;
						});
					},
					checked ? &st::mediaPlayerMenuCheck : nullptr);
			}
			submenu->addSeparator();
			const auto customChecked = (current.datePreset == SearchDatePreset::CustomRange)
				&& current.hasDateFilter();
			const auto customText = customChecked
				? (tr::jel_SearchFilterDateCustom(tr::now)
					+ u" ("_q + SearchDateFilterLabel(current) + ')')
				: tr::jel_SearchFilterDateCustom(tr::now);
			submenu->addAction(
				customText,
				[=] {
					ShowSearchDateRangeBox(controller, current, [=](SearchExtraFilters updated) {
						updated.sanitizeForContext(inPeer, isPrivateUser);
						if (onChanged) {
							onChanged(updated);
						}
					});
				},
				customChecked ? &st::mediaPlayerMenuCheck : &st::menuIconSchedule);
		},
		.submenuSt = &st::popupMenuWithIcons,
	});

	if (inPeer && isPrivateUser) {
		const auto senderText = (current.senderFilter == SearchSenderFilter::All)
			? tr::jel_SearchFilterSender(tr::now)
			: (tr::jel_SearchFilterSender(tr::now)
				+ u": "_q
				+ SearchSenderFilterLabel(current.senderFilter));
		addAction({
			.text = senderText,
			.handler = nullptr,
			.icon = &st::menuIconProfile,
			.fillSubmenu = [=](not_null<Ui::PopupMenu*> submenu) {
				for (const auto option : {
					SearchSenderFilter::All,
					SearchSenderFilter::MyMessages,
					SearchSenderFilter::PeerMessages,
				}) {
					const auto checked = (current.senderFilter == option);
					submenu->addAction(
						SearchSenderFilterLabel(option),
						[=] {
							apply([=](SearchExtraFilters &f) {
								f.senderFilter = option;
							});
						},
						checked ? &st::mediaPlayerMenuCheck : nullptr);
				}
			},
			.submenuSt = &st::popupMenuWithIcons,
		});
	} else if (inPeer && args.canChooseFromUser && args.onSetFromPeer) {
		const auto self = controller->session().user();
		const auto isMine = (args.fromPeer == self.get());
		const auto setFrom = args.onSetFromPeer;
		addAction({
			.text = tr::jel_SearchFilterSenderMine(tr::now),
			.handler = [=] {
				setFrom(isMine ? nullptr : self.get());
			},
			.icon = isMine ? &st::mediaPlayerMenuCheck : &st::menuIconProfile,
		});
	}

	if (!current.isDefault()) {
		addAction({ .isSeparator = true });
		addAction({
			.text = tr::jel_SearchFilterReset(tr::now),
			.handler = [=] {
				apply([](SearchExtraFilters &f) {
					f.reset();
				});
			},
			.icon = &st::menuIconClearAttention,
			.isAttention = true,
		});
	}

	menu->popup(globalPos);
}

} // namespace Jel
