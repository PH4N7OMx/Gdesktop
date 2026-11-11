#include "jel/ui/macos_sidebar.h"

#include "styles/palette.h"
#include "jel/ui/macos_interface.h"

#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "chat_helpers/compose/compose_show.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_common_session.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"
#include "styles/style_jel_styles.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QPainter>
#include <QtGui/QKeyEvent>
#include <algorithm>

namespace Jel::Ui {

struct MacosSidebar::State final : PeerListContentDelegateShow {
	State(MacosSidebar *owner, not_null<Window::SessionController*> window)
	: PeerListContentDelegateShow(window->uiShow())
	, owner(owner)
	, window(window)
	, scroll(owner)
	, title(owner, st::macosSettingsName)
	, back(owner, st::topBarSearch)
	, action(owner, st::topBarSearch)
	, search(owner, st::dialogsFilter, tr::lng_dlg_filter()) {
		back->setIconOverride(&st::topBarBack);
		back->setAccessibleName(tr::lng_go_back(tr::now));
		back->setClickedCallback([=] {
			goBack();
		});
		action->setClickedCallback([=] { actionClicked(); });
		search->customUpDown(true);
		search->changes() | rpl::on_next([=] {
			if (list) {
				scroll->scrollToY(0);
				list->searchQueryChanged(search->getLastText());
			}
		}, owner->lifetime());
		search->submits() | rpl::on_next([=] {
			if (list) list->submitted();
		}, owner->lifetime());
		owner->sizeValue() | rpl::on_next([=] { layout(); }, owner->lifetime());
		owner->paintRequest() | rpl::on_next([=](QRect clip) {
			auto p = QPainter(this->owner);
			p.fillRect(clip, tab == 3 ? st::boxDividerBg->c : st::dialogsBg->c);
			p.fillRect(QRect(0, 0, this->owner->width(), st::macosTopBarHeight), st::dialogsBg->c);
			p.fillRect(0, st::macosTopBarHeight - st::lineWidth,
				this->owner->width(), st::lineWidth, st::shadowFg->c);
		}, owner->lifetime());
		owner->hide();
	}
	~State() { clearPage(); }
	void clearPage() {
		clearing = true;
		pageLifetime.destroy();
		popup.reset();
		settings = nullptr;
		list = nullptr;
		setContent(nullptr);
		if (scroll->widget()) scroll->takeWidget<::Ui::RpWidget>().destroy();
		listController.reset();
		clearing = false;
	}
	void leaveSettings(Fn<void()> next) {
		// Defer destruction until the emitting section has returned to its event loop.
		const auto serial = ++navigationSerial;
		auto done = crl::guard(owner, [=] {
			crl::on_main(owner, [=] {
				if (serial == navigationSerial) next();
			});
		});
		if (settings) {
			const auto section = settings;
			section->checkBeforeClose(crl::guard(owner, [=] {
				if (section && serial == navigationSerial) section->sectionSaveChanges(done);
			}));
		} else {
			done();
		}
	}
	void goBack() {
		leaveSettings([=] {
			if (stack.size() > 1) {
				stack.pop_back();
				showSettingsPage(stack.back());
			} else {
				showNow(2);
			}
		});
	}
	void show(int next) {
		if (next < 0 || next > 3 || (next == tab && !owner->isHidden())) return;
		leaveSettings([=] { showNow(next); });
	}
	void showNow(int next) {
		if (next == 2) {
			clearPage();
			stack.clear();
			tab = next;
			owner->hide();
			changes.fire_copy(tab);
			return;
		}
		if (next == tab && !owner->isHidden()) return;
		clearPage();
		tab = next;
		stack.clear();
		search->clear();
		if (tab == 3) {
			stack.push_back(Settings::MainId());
			showSettingsPage(stack.back());
		} else {
			if (tab == 0) {
				auto contacts = std::make_unique<ContactsBoxController>(&window->session());
				contacts->setStyleOverrides(&st::macosContacts);
				contacts->setSortMode(ContactsBoxController::SortMode::Alphabet);
				contacts->setSectionHeadersShown(true);
				contacts->setStoriesShown(true);
				listController = std::move(contacts);
				action->setIconOverride(&st::menuIconAdd);
				action->setAccessibleName(tr::lng_profile_add_contact(tr::now));
			} else {
				listController = std::make_unique<::Calls::BoxController>(window);
				listController->setStyleOverrides(&st::macosRecentCalls);
				action->setIconOverride(&st::menuIconPhone);
				action->setAccessibleName(tr::lng_menu_calls(tr::now));
			}
			list = scroll->setOwnedWidget(object_ptr<PeerListContent>(scroll, listController.get()));
			setContent(list);
			list->setSearchMode(PeerListSearchMode::Enabled);
			listController->setDelegate(this);
			list->scrollToRequests() | rpl::on_next([=](::Ui::ScrollToRequest request) {
				scroll->scrollToY(request.ymin, request.ymax);
			}, pageLifetime);
			title->setText(tab == 0 ? tr::lng_menu_contacts(tr::now) : tr::lng_menu_calls(tr::now));
		}
		layout();
		owner->show();
		owner->raise();
		scroll->show();
		action->show();
		title->show();
		if (tab != 3) search->setFocus();
		changes.fire_copy(tab);
	}
	void showSettingsPage(Settings::Type type) {
		clearPage();
		auto section = type->create(scroll, window, scroll, rpl::single(Settings::Container::Section));
		settings = scroll->setOwnedWidget(std::move(section));
		settings->title() | rpl::on_next([=](const QString &text) {
			title->setText(text);
			layout();
		}, pageLifetime);
		settings->sectionShowOther() | rpl::on_next([=](Settings::Type target) {
			// Password/business editors with custom headers retain their own host.
			if (target->hasCustomTopBar()) { window->showSettings(target); return; }
			leaveSettings([=] {
				stack.push_back(target);
				showSettingsPage(target);
			});
		}, pageLifetime);
		settings->sectionShowBack() | rpl::on_next([=] {
			goBack();
		}, pageLifetime);
		settings->removeFromStack() | rpl::on_next([=](const std::vector<Settings::Type> &removed) {
			stack.erase(std::remove_if(stack.begin(), stack.end(), [&](const auto &entry) {
				return ranges::contains(removed, entry);
			}), stack.end());
			if (stack.empty()) stack.push_back(Settings::MainId());
		}, pageLifetime);
		action->setIconOverride(&st::topBarMenuToggle.icon);
		action->setAccessibleName(tr::lng_menu_settings(tr::now));
		layout();
		settings->showFinished();
		settings->setInnerFocus();
	}
	void layout() {
		const auto width = owner->width();
		const auto top = st::macosTopBarHeight;
		const auto searchable = tab != 3;
		back->setVisible(tab == 3 && stack.size() > 1);
		const auto scaled = [](int value) { return style::ConvertScale(value); };
		const auto controlsInset = MacosTitleControlsInset(owner);
		back->moveToLeft(controlsInset, 0);
		action->moveToRight(scaled(4), 0);
		const auto leading = controlsInset + (back->isHidden() ? 0 : back->width());
		const auto trailing = action->width() + scaled(4);
		const auto labelWidth = std::max(0, width - leading - trailing);
		title->resizeToNaturalWidth(labelWidth);
		title->moveToLeft(leading + (labelWidth - title->width()) / 2, (top - title->height()) / 2);
		search->setVisible(searchable);
		if (searchable) search->setGeometry(scaled(14), top + scaled(7), std::max(0, width - scaled(28)), search->height());
		const auto contentTop = top + (searchable ? search->height() + scaled(14) : 0);
		scroll->setGeometry(0, contentTop, width, std::max(0, owner->height() - contentTop));
		if (list) list->resizeToWidth(scroll->scrollWidth());
		if (settings) settings->resizeToWidth(scroll->scrollWidth());
		updateVisible();
	}
	void updateVisible() {
		if (clearing) return;
		const auto top = scroll->scrollTop();
		const auto bottom = top + scroll->scrollHeight();
		if (list) list->setVisibleTopBottom(top, bottom);
		if (settings) settings->setVisibleTopBottom(top, bottom);
		if (listController && bottom + 200 >= (list ? list->height() : 0)) listController->loadMoreRows();
	}
	bool handleKey(QKeyEvent *event) {
		const auto key = event->key();
		if (key == Qt::Key_Escape) {
			if (tab != 3 && !search->getLastText().isEmpty()) search->clear();
			else goBack();
		} else if (list && (key == Qt::Key_Down || key == Qt::Key_Up)) {
			list->selectSkip(key == Qt::Key_Down ? 1 : -1);
		} else if (list && (key == Qt::Key_PageDown || key == Qt::Key_PageUp)) {
			list->selectSkipPage(scroll->scrollHeight(), key == Qt::Key_PageDown ? 1 : -1);
		} else {
			return false;
		}
		event->accept();
		return true;
	}
	void actionClicked() {
		if (tab == 0) { window->showAddContact(); return; }
		if (tab == 1) { ::Calls::ShowCallsBox(window, true); return; }
		if (!settings) return;
		popup = base::make_unique_q<::Ui::PopupMenu>(owner, st::popupMenuWithIcons);
		settings->fillTopBarMenu(::Ui::Menu::CreateAddActionCallback(popup.get()));
		if (!popup->empty()) popup->popup(action->mapToGlobal(QPoint(0, action->height())));
	}
	void peerListSetTitle(rpl::producer<QString> value) override {
		std::move(value) | rpl::on_next([=](const QString &text) { title->setText(text); layout(); }, pageLifetime);
	}
	void peerListSetDescription(object_ptr<::Ui::FlatLabel> value) override {
		PeerListContentDelegate::peerListSetDescription(std::move(value));
	}
	void peerListSetSearchMode(PeerListSearchMode mode) override {
		PeerListContentDelegate::peerListSetSearchMode(mode);
	}
	void peerListScrollToTop() override { scroll->scrollToY(0); }

	MacosSidebar *owner;
	not_null<Window::SessionController*> window;
	object_ptr<::Ui::ScrollArea> scroll;
	object_ptr<::Ui::FlatLabel> title;
	object_ptr<::Ui::IconButton> back;
	object_ptr<::Ui::IconButton> action;
	object_ptr<::Ui::InputField> search;
	std::unique_ptr<PeerListController> listController;
	QPointer<PeerListContent> list;
	QPointer<Settings::AbstractSection> settings;
	base::unique_qptr<::Ui::PopupMenu> popup;
	std::vector<Settings::Type> stack;
	rpl::lifetime pageLifetime;
	rpl::event_stream<int> changes;
	uint64 navigationSerial = 0;
	bool clearing = false;
	int tab = 2;
};

MacosSidebar::MacosSidebar(QWidget *parent, not_null<Window::SessionController*> controller)
: RpWidget(parent), _state(std::make_unique<State>(this, controller)) {
	_state->scroll->scrolls() | rpl::on_next([=] { _state->updateVisible(); }, lifetime());
}
MacosSidebar::~MacosSidebar() = default;
void MacosSidebar::keyPressEvent(QKeyEvent *event) {
	if (!_state->handleKey(event)) RpWidget::keyPressEvent(event);
}
void MacosSidebar::showTab(int tab) { _state->show(tab); }
int MacosSidebar::currentTab() const { return _state->tab; }
rpl::producer<int> MacosSidebar::tabChanges() const { return _state->changes.events_starting_with_copy(_state->tab); }

} // namespace Jel::Ui
