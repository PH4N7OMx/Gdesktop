#include "jel/ui/macos_interface.h"
#include "jel/ui/macos_sidebar.h"

#include "jel/jel_settings.h"
#include "jel/features/message_shot/message_shot.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"
#include "ui/style/style_palette_colorizer.h"
#include "styles/style_boxes.h"
#include "styles/style_jel_styles.h"
#include "styles/style_dialogs.h"
#include "styles/style_dialogs_layout.h"
#include "styles/style_chat_style.h"
#include "styles/style_media_view.h"
#include "styles/style_media_player.h"
#include "styles/style_chat.h"
#include "styles/style_calls.h"
#include "styles/style_intro.h"
#include "styles/style_wallet.h"
#include "styles/style_window.h"
#include "styles/style_credits.h"
#include "styles/style_edit_peer_members.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_info_profile_top_bar.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/style/style_core_palette.h"

#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "ui/unread_badge.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include <QtGui/QFontDatabase>
#include "ui/wrap/vertical_layout.h"
#include "ui/integration.h"
#include "ui/platform/ui_platform_window_title.h"
#include "window/window_session_controller.h"

#include <QtGui/QPainter>
#include <QAccessible>
#include "base/flat_set.h"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace Jel::Ui {
namespace {

constexpr auto kMacosSelectionDuration = 200;

bool Enabled = false;
bool IconsEnabled = false;
base::flat_set<const style::MediaSlider*> NativeSliders;

using TitleControl = ::Ui::Platform::TitleControl;

class TrafficLightButton final : public ::Ui::AbstractButton {
public:
	TrafficLightButton(QWidget *parent, TitleControl control, int height, Fn<void()> hover)
	: AbstractButton(parent), _control(control), _hover(std::move(hover)) {
		resize(style::ConvertScale(control == TitleControl::Close ? 24 : 20), height);
		setPointerCursor(false);
	}
	void setAppearance(bool active, bool maximized, bool groupOver) {
		_active = active;
		_maximized = maximized;
		_groupOver = groupOver;
		update();
	}
protected:
	void onStateChanged(State was, StateChangeSource source) override {
		AbstractButton::onStateChanged(was, source);
		if (_hover) _hover();
		update();
	}
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto scale = style::ConvertScale(100) / 100.;
		p.scale(scale, scale);
		const auto center = QPointF(_control == TitleControl::Close ? 14 : 10, height() / (2. * scale));
		auto color = !_active && !_groupOver ? QColor(0xb9, 0xb9, 0xbb)
			: _control == TitleControl::Close ? QColor(0xff, 0x5f, 0x57)
			: _control == TitleControl::Minimize ? QColor(0xfe, 0xbc, 0x2e)
			: QColor(0x28, 0xc8, 0x40);
		if (isDown()) color = color.darker(115);
		p.setPen(QPen(color.darker(112), 0.5));
		p.setBrush(color);
		p.drawEllipse(center, 6., 6.);
		if (!_groupOver) return;
		p.setPen(QPen(color.darker(260), 1.1, Qt::SolidLine, Qt::RoundCap));
		const auto line = [&](QPointF a, QPointF b) { p.drawLine(center + a, center + b); };
		if (_control == TitleControl::Close) {
			line({-2., -2.}, {2., 2.});
			line({-2., 2.}, {2., -2.});
		} else if (_control == TitleControl::Minimize) {
			line({-3., 0.}, {3., 0.});
		} else {
			p.setBrush(color.darker(260));
			p.setPen(Qt::NoPen);
			if (_maximized) {
				p.drawPolygon(QPolygonF{center + QPointF(-1., -1.), center + QPointF(-4., -1.), center + QPointF(-1., -4.)});
				p.drawPolygon(QPolygonF{center + QPointF(1., 1.), center + QPointF(4., 1.), center + QPointF(1., 4.)});
			} else {
				p.drawPolygon(QPolygonF{center + QPointF(-3., -3.), center + QPointF(1., -3.), center + QPointF(-3., 1.)});
				p.drawPolygon(QPolygonF{center + QPointF(3., 3.), center + QPointF(-1., 3.), center + QPointF(3., -1.)});
			}
		}
	}
private:
	TitleControl _control;
	Fn<void()> _hover;
	bool _active = true;
	bool _maximized = false;
	bool _groupOver = false;

};

class TrafficLightButtons final : public ::Ui::Platform::AbstractTitleButtons {
public:
	object_ptr<::Ui::AbstractButton> create(
			not_null<QWidget*> parent, TitleControl control,
			const style::WindowTitle &st) override {
		auto button = object_ptr<TrafficLightButton>(parent, control, st.height, [=] { refresh(); });
		_buttons[index(control)] = button.data();
		return button;
	}
	void updateState(bool active, bool maximized, const style::WindowTitle &) override {
		_active = active;
		_maximized = maximized;
		const auto integration = &::Ui::Integration::Instance();
		if (_buttons[0]) _buttons[0]->setAccessibleName(integration->phraseButtonClose());
		if (_buttons[1]) _buttons[1]->setAccessibleName(integration->phraseMinimize());
		if (_buttons[2]) _buttons[2]->setAccessibleName(maximized ? integration->phraseRestore() : integration->phraseMaximize());
		refresh();
	}
	void notifySynteticOver(TitleControl, bool) override { refresh(); }
private:
	static int index(TitleControl control) {
		return control == TitleControl::Close ? 0 : control == TitleControl::Minimize ? 1 : 2;
	}
	void refresh() {
		const auto over = ranges::any_of(_buttons, [](const auto &button) { return button && button->isOver(); });
		for (const auto &button : _buttons) {
			if (button) button->setAppearance(_active, _maximized, over);
		}
	}
	std::array<QPointer<TrafficLightButton>, 3> _buttons;
	bool _active = true;
	bool _maximized = false;

};

class MacosNavigationButton final : public ::Ui::IconButton {
public:
	using IconButton::IconButton;
	void setSelected(bool selected) {
		if (_selected == selected) return;
		_selected = selected;
		auto event = QAccessibleEvent(this, selected ? QAccessible::SelectionAdd : QAccessible::SelectionRemove);
		QAccessible::updateAccessibility(&event);
	}
	::Ui::AccessibilityState accessibilityState() const override {
		auto state = IconButton::accessibilityState();
		state.selectable = true;
		state.selected = _selected;
		return state;
	}
private:
	bool _selected = false;

};

template <typename Type>
void Replace(const Type &target, const Type &replacement) {
	// Keep original icon owners alive while style exports use replacements.
	static auto originals = std::vector<std::unique_ptr<Type>>();
	auto &value = const_cast<Type&>(target);
	originals.push_back(std::make_unique<Type>(std::move(value)));
	value = replacement;
}

} // namespace

QString MacosFontFamily(const QString &configured) {
	if (!JelSettings::getInstance().macosInterface() || !configured.isEmpty()) return configured;
#ifdef Q_OS_MAC
	return style::SystemFontTag();
#else
	#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const auto families = QFontDatabase::families();
#else
	const auto families = QFontDatabase().families();
#endif
	for (const auto &candidate : { u"SF Pro Text"_q, u"SF Pro Display"_q,
			u"Helvetica Neue"_q, u"Inter"_q, u"Segoe UI"_q }) {
		if (families.contains(candidate, Qt::CaseInsensitive)) return candidate;
	}
	return style::SystemFontTag();
#endif
}

std::unique_ptr<style::PeerList> PrepareMacosPeerListStyle(const style::PeerList &original) {
	const auto &item = original.item;
	if (!Enabled || item.height < style::ConvertScale(50) || item.height > style::ConvertScale(58)
		|| item.photoSize < style::ConvertScale(40) || item.photoSize > style::ConvertScale(46)) {
		return nullptr;
	}
	auto native = std::make_unique<style::PeerList>(original);
	const auto &source = st::macosPeerItem;
	native->item.height = source.height;
	native->item.photoSize = source.photoSize;
	native->item.photoPosition = source.photoPosition;
	native->item.namePosition = source.namePosition;
	native->item.statusPosition = source.statusPosition;
	native->item.nameStyle = source.nameStyle;
	native->item.checkbox.imageRadius = source.checkbox.imageRadius;
	native->item.checkbox.imageSmallRadius = source.checkbox.imageSmallRadius;
	return native;
}

bool MacosContinuousSliderEnabled(const style::MediaSlider &style) {
	return Enabled && NativeSliders.contains(&style);
}

void StartMacosInterface() {
	const auto &settings = JelSettings::getInstance();
	Enabled = settings.macosInterface();
	if (Enabled) {
		::Ui::SetMacosSwitches(true);
		Replace(st::callName.style, st::macosCallName);
		Replace(st::callName.maxHeight, st::macosCallName.font->height);
		Replace(st::callStatus.style, st::macosCallStatus);
		{
			auto layout = st::callBodyLayout;
			layout.photoSize = style::ConvertScale(120);
			layout.nameTop = layout.photoTop + layout.photoSize + style::ConvertScale(32);
			layout.statusTop = layout.nameTop + st::macosCallName.font->height + style::ConvertScale(6);
			layout.participantsTop = layout.statusTop + style::ConvertScale(40);
			layout.height = layout.statusTop + st::macosCallStatus.font->height + style::ConvertScale(10);
			layout.muteSize = style::ConvertScale(24);
			layout.muteStroke = style::ConvertScale(2);
			layout.mutePosition = QPoint(style::ConvertScale(100), style::ConvertScale(95));
			Replace(st::callBodyLayout, layout);
			layout = st::callBodyWithPreview;
			layout.statusTop = layout.nameTop + st::macosCallName.font->height + style::ConvertScale(6);
			layout.participantsTop = layout.statusTop + style::ConvertScale(40);
			layout.height = layout.statusTop + st::macosCallStatus.font->height + style::ConvertScale(10);
			Replace(st::callBodyWithPreview, layout);
		}

		for (const auto button : { &st::callAnswer, &st::callStartVideo, &st::callHangup,
				&st::callCancel, &st::callMicrophoneMute, &st::callMicrophoneUnmute,
				&st::callCameraMute, &st::callCameraUnmute, &st::callAddPeople,
				&st::callScreencastOn, &st::callScreencastOff }) {
			Replace(button->bgSize, style::ConvertScale(50));
			Replace(button->bgPosition, QPoint((button->button.width - button->bgSize) / 2,
				style::ConvertScale(9)));
			Replace(button->button.rippleAreaPosition, button->bgPosition);
			Replace(button->button.rippleAreaSize, button->bgSize);
			Replace(button->label.style, st::macosCallLabel);
			Replace(button->button.iconPosition, QPoint(-1,
				button->bgPosition.y() + (button->bgSize - button->button.icon.height()) / 2));
		}

		Replace(st::windowMinHeight, st::macosWindowMinHeight);
		Replace(st::defaultCheck, st::macosCheck);
		Replace(st::defaultCheck.duration, kMacosSelectionDuration);
		Replace(st::defaultCheckbox.style, st::macosCheckboxStyle);
		Replace(st::defaultCheckbox.textPosition, QPoint(st::defaultCheckbox.textPosition.x(), 0));
		::Ui::SetMacosRoundButtons(true);
		for (const auto slider : { &st::settingsScale, &st::autoDownloadLimitSlider, &st::localStorageLimitSlider }) {
			Replace(slider->seekSize, st::macosSettingsSliderSize);
			Replace(slider->inactiveFg, st::windowSubTextFg);
			Replace(slider->inactiveFgOver, st::windowSubTextFg);
			Replace(slider->activeFg, st::windowActiveTextFg);
			Replace(slider->activeFgOver, st::windowActiveTextFg);
			NativeSliders.emplace(slider);
		}
		Replace(st::introNextButton, st::macosAuthNextButton);
		Replace(st::introTitle.style, st::macosAuthTitle);
		Replace(st::introCoverTitle.style, st::macosAuthTitle);
		Replace(st::introQrTitle.style, st::macosAuthQrTitle);
		Replace(st::introQrMaxSize, st::macosAuthQrPixels);
		Replace(st::introQrStep, st::macosAuthQrStep);
		Replace(st::introQrStep.maxHeight, 2 * st::macosAuthQrStep.style.font->height);
		{
			auto button = st::introFragmentButton;
			button.width = st::macosAuthNextButton.width;
			button.height = st::macosAuthNextButton.height;
			button.style = st::macosAuthNextButton.style;
			button.textTop = st::macosAuthNextButton.textTop;
			button.fullRadius = true;
			button.iconPosition.setY((button.height - button.icon.height()) / 2);
			Replace(st::introFragmentButton, button);
		}
		Replace(st::introCountry, st::macosAuthCountry);
		Replace(st::introCountryCode, st::macosAuthCountryCode);
		Replace(st::introPhone, st::macosAuthPhone);
		const auto phoneFont = st::macosAuthInput.style.font->monospace();
		Replace(st::introCountryCode.style.font, phoneFont);
		Replace(st::introPhone.style.font, phoneFont);
		Replace(st::introPhone.placeholderFont, phoneFont);
		Replace(st::introName, st::macosAuthInput);
		Replace(st::introPassword, st::macosAuthInput);
		Replace(st::introPhoneTop, 0);
		Replace(st::introCountryIconPosition, QPoint(style::ConvertScale(10),
			(st::macosAuthCountry.heightMin - st::introCountryIcon.height()) / 2));
		Replace(st::introDescription.style, st::macosAuthInfo);
		Replace(st::introCoverDescription.style, st::macosAuthInfo);
		Replace(st::introPasswordHint.style, st::macosAuthInfo);
		Replace(st::introError.style, st::macosAuthInfo);
		Replace(st::introErrorCentered.style, st::macosAuthInfo);
		Replace(st::defaultBoxButton, st::macosModalButton);
		Replace(st::defaultBoxButtonTextStyle, st::macosModalButton.style);
		Replace(st::boxTitleFont, st::macosModalTitle.font);
		Replace(st::boxTitle.style, st::macosModalTitle);
		Replace(st::boxTitleHeight, st::macosTopBarHeight);
		Replace(st::boxTitlePosition, QPoint(style::ConvertScale(20),
			(st::macosTopBarHeight - st::macosModalTitle.font->height) / 2));
		for (const auto target : { &st::defaultBox, &st::layerBox }) {
			auto box = *target;
			box.button = st::macosModalButton;
			box.buttonHeight = box.button.height;
			box.buttonPadding = QMargins(style::ConvertScale(10), style::ConvertScale(10),
				style::ConvertScale(20), style::ConvertScale(10));
			box.title.style = st::macosModalTitle;
			Replace(*target, box);
		}
		for (const auto target : { &st::boxTitleClose, &st::boxTitleMenu }) {
			auto button = *target;
			button.width = button.height = st::macosTopBarHeight;
			button.rippleAreaPosition.setY((button.height - button.rippleAreaSize) / 2);
			Replace(*target, button);
		}
		::Ui::SetInputFieldStyleOverride([](const style::InputField &original)
				-> std::unique_ptr<style::InputField> {
			if (original.borderRadius || !original.borderActive || original.placeholderScale <= 0.
				|| original.textMargins.left() || original.textMargins.right()) return nullptr;
			auto field = std::make_unique<style::InputField>(original);
			field->borderRadius = style::ConvertScale(20);
			field->border = style::ConvertScale(1);
			field->borderActive = field->border;
			field->textMargins.setLeft(style::ConvertScale(14));
			field->textMargins.setRight(style::ConvertScale(14));
			return field;
		});
		Replace(st::infoProfileTabsStrip, st::macosProfileTabsStrip);
		{
			auto tabs = st::macosSegmentedSlider;
			tabs.labelTop = (tabs.height - tabs.labelStyle.font->height) / 2;
			Replace(st::walletTabsSlider, tabs);
			Replace(st::creditsHistoryTabsSlider, tabs);
			Replace(st::walletRowsTitle.labelStyle, tabs.labelStyle);
		}
		Replace(st::bubbleRadiusSmall, st::macosBubbleRadiusSmall);
		Replace(st::boxRadius, style::ConvertScale(10));
		Replace(st::infoProfileTopBarPhotoSize, style::ConvertScale(120));
		const auto profileShift = style::ConvertScale(40);
		Replace(st::infoProfileTopBarTitleTop, st::infoProfileTopBarTitleTop + profileShift);
		Replace(st::infoProfileTopBarStatusTop, st::infoProfileTopBarStatusTop + profileShift);
		Replace(st::infoProfileTopBarHeightMax, st::infoProfileTopBarHeightMax + profileShift);
		Replace(st::infoProfileTopBarNoActionsHeightMax, st::infoProfileTopBarNoActionsHeightMax + profileShift);
		Replace(st::infoProfileTopBarStep1, st::infoProfileTopBarStep1 + profileShift);
		Replace(st::infoProfileTopBarStep2, st::infoProfileTopBarStep2 + profileShift);
#ifndef Q_OS_MAC
		::Ui::Platform::SetTitleControlsOverride({
			.left = { TitleControl::Close, TitleControl::Minimize, TitleControl::Maximize },
		}, [] { return std::make_unique<TrafficLightButtons>(); });
		Replace(st::defaultWindowTitle, st::macosWindowTitle);
#endif
		Replace(st::defaultDialogRow, st::macosDialogRow);
		Replace(st::taggedDialogRow, st::macosTaggedDialogRow);
		Replace(st::forumDialogRow, st::macosForumDialogRow);
		Replace(st::taggedForumDialogRow, st::macosTaggedForumDialogRow);
		Replace(st::communityInfoDialogRow, st::macosCommunityInfoDialogRow);
		Replace(st::communityInfoTaggedDialogRow, st::macosCommunityInfoTaggedDialogRow);
		Replace(st::communityInfoForumDialogRow, st::macosCommunityInfoForumDialogRow);
		Replace(st::communityInfoTaggedForumDialogRow, st::macosCommunityInfoTaggedForumDialogRow);
		Replace(st::communityRequestableListItem, st::macosCommunityRequestableListItem);
		Replace(st::communityRequestableList.item, st::macosCommunityRequestableListItem);
		Replace(st::communityInfoRequestableListItem, st::macosCommunityInfoRequestableListItem);
		Replace(st::communityInfoRequestableList.item, st::macosCommunityInfoRequestableListItem);
		Replace(st::dialogsRowHeight, st::macosDialogRow.height);
		Replace(st::dialogsTextFont, st::macosDialogsFont);
		Replace(st::dialogsTextStyle, st::macosDialogsTextStyle);
		Replace(st::dialogsUnreadFont, st::macosDialogsUnreadFont);
		Replace(st::dialogsUnreadHeight, style::ConvertScale(20));
		Replace(st::dialogsUnreadPadding, style::ConvertScale(4));
		Replace(st::dialogsDateFont, st::macosDialogsDateFont);
		Replace(st::contactsStatusFont, st::macosPeerStatusStyle.font);
		for (const auto button : { &st::callReDial, &st::callCameraReDial }) {
			auto native = *button;
			native.height = st::macosRecentCalls.item.height;
			native.rippleAreaPosition.setY((native.height - native.rippleAreaSize) / 2);
			Replace(*button, native);
		}
		Replace(st::dialogsFilter, st::macosDialogsFilter);
		Replace(st::msgPadding, st::macosMessagePadding);
		Replace(st::historyComposeField, st::macosComposeField);
		Replace(st::settingsButton, st::macosSettingsButton);
		Replace(st::settingsButtonNoIcon, st::macosSettingsButtonNoIcon);
		const auto settingsStyle = [&](const style::SettingsButton &target, bool icon) {
			auto replacement = target;
			replacement.padding = icon ? st::macosSettingsButton.padding : st::macosSettingsButtonNoIcon.padding;
			replacement.style = st::macosSettingsButton.style;
			replacement.height = st::macosSettingsButton.height;
			replacement.iconLeft = st::macosSettingsButton.iconLeft;
			replacement.toggleSkip = st::macosSettingsButton.toggleSkip;
			replacement.rightLabel = st::macosSettingsButton.rightLabel;
			Replace(target, replacement);
		};
		settingsStyle(st::settingsButtonLight, true);
		settingsStyle(st::settingsButtonLightNoIcon, false);
		settingsStyle(st::settingsAttentionButton, false);
		settingsStyle(st::settingsAttentionButtonWithIcon, true);
		settingsStyle(st::settingsButtonNoIconLocked, false);
		settingsStyle(st::settingsOptionDisabled, false);
		settingsStyle(st::settingsButtonDisabledWithIcon, true);

		auto menu = st::macosMenu;
		auto withIcons = st::macosMenuWithIcons;
		const auto free = std::max(0, style::ConvertScale(28) - menu.itemStyle.font->height);
		menu.itemPadding.setTop(free / 2);
		menu.itemPadding.setBottom(free - free / 2);
		withIcons.itemPadding.setTop(free / 2);
		withIcons.itemPadding.setBottom(free - free / 2);
		Replace(st::defaultMenu, menu);
		Replace(st::menuWithIcons, withIcons);
		// Popup/dropdown styles store their menu by value.
		Replace(st::defaultPopupMenu, st::macosPopupMenu);
		Replace(st::popupMenuWithIcons, st::macosPopupMenuWithIcons);
		Replace(st::defaultPopupMenu.menu, menu);
		Replace(st::popupMenuWithIcons.menu, withIcons);
		Replace(st::defaultDropdownMenu.menu, menu);
		Replace(st::dropdownMenuWithIcons.menu, withIcons);
		auto attention = menu;
		attention.itemFg = st::attentionButtonFg;
		attention.itemFgOver = st::attentionButtonFgOver;
		Replace(st::menuAttention, attention);
		attention = withIcons;
		attention.itemFg = st::attentionButtonFg;
		attention.itemFgOver = st::attentionButtonFgOver;
		Replace(st::menuWithIconsAttention, attention);
	}
	IconsEnabled = (settings.interfaceIcons() == 2)
		|| ((settings.interfaceIcons() == 0) && Enabled);
	if (IconsEnabled) {
		Replace(st::mediaPlayerNextButton.icon, st::macosPlayerNext);
		Replace(st::mediaPlayerNextDisabledIcon, st::macosPlayerNextDisabled);
		Replace(st::mediaPlayerPreviousButton.icon, st::macosPlayerPrevious);
		Replace(st::mediaPlayerPreviousDisabledIcon, st::macosPlayerPreviousDisabled);
		Replace(st::mediaPlayerRepeatButton.icon, st::macosPlayerRepeat);
		Replace(st::mediaPlayerRepeatDisabledIcon, st::macosPlayerRepeatDisabled);
		Replace(st::mediaPlayerRepeatDisabledIconOver, st::macosPlayerRepeatDisabledOver);
		Replace(st::mediaPlayerRepeatOneIcon, st::macosPlayerRepeatOne);
		Replace(st::mediaPlayerVolumeToggle.icon, st::macosPlayerVolume);
		Replace(st::mediaPlayerVolumeIcon0, st::macosPlayerMuted);
		Replace(st::mediaPlayerVolumeIcon1, st::macosPlayerVolume);
		Replace(st::mediaPlayerClose.icon, st::macosPlayerClose);
		Replace(st::mediaPlayerClose.iconOver, st::macosPlayerCloseOver);
		Replace(st::mediaPlayerOrderButton.icon, st::macosPlayerOrder);
		Replace(st::mediaPlayerReverseIcon, st::macosPlayerOrder);
		Replace(st::mediaPlayerReverseDisabledIcon, st::macosPlayerOrderDisabled);
		Replace(st::mediaPlayerReverseDisabledIconOver, st::macosPlayerOrderDisabledOver);
		Replace(st::mediaPlayerShuffleIcon, st::macosPlayerShuffle);
		Replace(st::mediaPlayerOrderIconReverse, st::macosPlayerOrderMenuReverse);
		Replace(st::mediaPlayerOrderIconReverseActive, st::macosPlayerOrderMenuReverseActive);
		Replace(st::mediaPlayerOrderIconShuffle, st::macosPlayerOrderMenuShuffle);
		Replace(st::mediaPlayerOrderIconShuffleActive, st::macosPlayerOrderMenuShuffleActive);
		for (const auto button : { &st::mediaPlayerNextButton, &st::mediaPlayerPreviousButton,
				&st::mediaPlayerRepeatButton, &st::mediaPlayerOrderButton,
				&st::mediaPlayerVolumeToggle, &st::mediaPlayerClose }) {
			Replace(button->iconPosition, QPoint(-1, -1));
		}
		Replace(st::callTooltipMutedIcon, st::macosCallMicrophoneOff);
		Replace(st::callMicrophoneMute.button.icon, st::macosCallMicrophone);
		Replace(st::callMicrophoneUnmute.button.icon, st::macosCallMicrophoneActive);
		Replace(st::callCameraMute.button.icon, st::macosCallVideo);
		Replace(st::callCameraUnmute.button.icon, st::macosCallVideoActive);
		Replace(st::callStartVideo.button.icon, st::macosCallVideo);
		Replace(st::callScreencastOn.button.icon, st::macosCallScreen);
		Replace(st::callScreencastOff.button.icon, st::macosCallScreenActive);
		Replace(st::callAddPeople.button.icon, st::macosCallPeople);
		for (const auto button : { &st::callStartVideo, &st::callMicrophoneMute,
				&st::callMicrophoneUnmute, &st::callCameraMute, &st::callCameraUnmute,
				&st::callScreencastOn, &st::callScreencastOff, &st::callAddPeople }) {
			Replace(button->button.iconPosition, QPoint(-1,
				button->bgPosition.y() + (button->bgSize - button->button.icon.height()) / 2));
		}
		Replace(st::defaultCheckboxIcon, st::macosCheckMark);
		Replace(st::defaultCheck.icon, st::macosCheckMark);
		Replace(st::historyAttach, st::macosHistoryAttach);
		Replace(st::topBarSearch, st::macosTopBarSearch);
		Replace(st::topBarCall, st::macosTopBarCall);
		Replace(st::topBarMenuToggle, st::macosTopBarMenuToggle);
		Replace(st::historyAttachEmojiInner, st::macosHistoryAttachEmojiInner);
		Replace(st::historySendIcon, st::macosHistorySendIcon);
		Replace(st::historySendIconOver, st::macosHistorySendIconOver);
		Replace(st::topBarBack, st::macosTopBarBack);
		Replace(st::mediaviewLeft, st::macosGalleryprev);
		Replace(st::mediaviewRight, st::macosGallerynext);
		Replace(st::mediaviewSave, st::macosGallerysave);
		Replace(st::mediaviewShare, st::macosGalleryshare);
		Replace(st::mediaviewMore, st::macosGallerymore);
		Replace(st::mediaviewRotate, st::macosGalleryrotate);
		Replace(st::defaultComposeIcons, st::macosComposeIcons);
		for (const auto pan : { &st::defaultEmojiPan, &st::statusEmojiPan,
				&st::backgroundEmojiPan, &st::userpicBuilderEmojiPan, &st::reactPanelEmojiPan }) {
			Replace(pan->icons, st::macosComposeIcons);
		}
		for (const auto pan : { &st::storiesEmojiPan, &st::mediaviewEmojiPan, &st::storiesReactionsPan }) {
			Replace(pan->icons, st::macosStoriesComposeIcons);
		}
		Replace(st::dialogsSentIcon, st::macosDialogsSent);
		Replace(st::dialogsReceivedIcon, st::macosDialogsReceived);
		Replace(st::dialogsPinnedIcon, st::macosDialogsPinned);
		Replace(st::dialogsMuteIcon, st::macosDialogsMuted);
		Replace(st::historySendingIcon, st::macosHistorySending);
		Replace(st::historySendingInvertedIcon, st::macosHistorySendingInverted);
		Replace(st::historySentIcon, st::macosHistorySent);
		Replace(st::historyReceivedIcon, st::macosHistoryReceived);
		Replace(st::historySentSelectedIcon, st::macosHistorySentSelected);
		Replace(st::historyReceivedSelectedIcon, st::macosHistoryReceivedSelected);
		Replace(st::menuIconSettings, st::macosMenuIconSettings);
		Replace(st::menuIconProfile, st::macosMenuIconProfile);
		Replace(st::menuIconNotifications, st::macosMenuIconNotifications);
		Replace(st::menuIconPalette, st::macosMenuIconPalette);
		Replace(st::menuIconPhone, st::macosMenuIconPhone);
		Replace(st::menuIconSearch, st::macosMenuIconSearch);
		Replace(st::menuIconReply, st::macosMenuIconReply);
		Replace(st::menuIconCopy, st::macosMenuIconCopy);
		Replace(st::menuIconForward, st::macosMenuIconForward);
		Replace(st::menuIconDelete, st::macosMenuIconDelete);
		Replace(st::menuIconEdit, st::macosMenuIconEdit);
		Replace(st::menuIconPin, st::macosMenuIconPin);
		Replace(st::menuIconArchive, st::macosMenuIconArchive);
		Replace(st::menuIconUnpin, st::macosMenuIconUnpin);
		Replace(st::menuIconUnarchive, st::macosMenuIconUnarchive);
		Replace(st::menuIconMarkRead, st::macosMenuIconMarkRead);
		Replace(st::menuIconMarkUnread, st::macosMenuIconMarkUnread);
		Replace(st::menuIconMute, st::macosMenuIconMute);
		Replace(st::menuIconUnmute, st::macosMenuIconUnmute);
		Replace(st::menuIconMuteFor, st::macosMenuIconMuteFor);
		Replace(st::menuIconSoundOn, st::macosMenuIconSoundOn);
		Replace(st::menuIconSoundOff, st::macosMenuIconSoundOff);
		Replace(st::menuIconInfo, st::macosMenuIconInfo);
		Replace(st::menuIconViewReplies, st::macosMenuIconViewReplies);
		Replace(st::menuIconSelect, st::macosMenuIconSelect);
		Replace(st::menuIconSaveImage, st::macosMenuIconSaveImage);
		Replace(st::menuIconDownload, st::macosMenuIconDownload);
		Replace(st::menuIconLink, st::macosMenuIconLink);
		Replace(st::menuIconLinks, st::macosMenuIconLinks);
		Replace(st::menuIconBlock, st::macosMenuIconBlock);
		Replace(st::menuIconUnblock, st::macosMenuIconUnblock);
		Replace(st::menuIconReport, st::macosMenuIconReport);
		Replace(st::menuIconReportAntiSpam, st::macosMenuIconReportAntiSpam);
		Replace(st::menuIconClear, st::macosMenuIconClear);
		Replace(st::menuIconCreatePoll, st::macosMenuIconCreatePoll);
		Replace(st::menuIconCreateTodoList, st::macosMenuIconCreateTodoList);
		Replace(st::menuIconArticle, st::macosMenuIconArticle);
		Replace(st::menuIconGif, st::macosMenuIconGif);
		Replace(st::menuIconStickers, st::macosMenuIconStickers);
		Replace(st::menuIconEmoji, st::macosMenuIconEmoji);
		Replace(st::menuIconFile, st::macosMenuIconFile);
		Replace(st::menuIconPhoto, st::macosMenuIconPhoto);
		Replace(st::menuIconLeave, st::macosMenuIconLeave);
		Replace(st::menuIconSchedule, st::macosMenuIconSchedule);
		Replace(st::menuIconReschedule, st::macosMenuIconReschedule);
		Replace(st::menuIconSend, st::macosMenuIconSend);
		Replace(st::menuIconWhenOnline, st::macosMenuIconWhenOnline);
		Replace(st::menuIconTranslate, st::macosMenuIconTranslate);
		Replace(st::menuIconShare, st::macosMenuIconShare);
		Replace(st::menuIconGroups, st::macosMenuIconGroups);
		Replace(st::menuIconChannel, st::macosMenuIconChannel);
		Replace(st::menuIconNightMode, st::macosMenuIconNightMode);
		Replace(st::menuIconLock, st::macosMenuIconLock);
		Replace(st::menuIconStats, st::macosMenuIconStats);
		Replace(st::menuIconReactions, st::macosMenuIconReactions);
		Replace(st::menuIconRetractVote, st::macosMenuIconRetractVote);
		Replace(st::menuIconPromote, st::macosMenuIconPromote);
		Replace(st::menuIconPermissions, st::macosMenuIconPermissions);
		Replace(st::menuIconExpand, st::macosMenuIconExpand);
		Replace(st::menuIconCollapse, st::macosMenuIconCollapse);
		Replace(st::menuIconAddToFolder, st::macosMenuIconAddToFolder);
		Replace(st::menuIconShowInFolder, st::macosMenuIconShowInFolder);
		Replace(st::menuIconFave, st::macosMenuIconFave);
		Replace(st::menuIconUnfave, st::macosMenuIconUnfave);
		Replace(st::menuIconAdd, st::macosMenuIconAdd);
		Replace(st::menuIconBoosts, st::macosMenuIconBoosts);
		Replace(st::menuIconSpoiler, st::macosMenuIconSpoiler);
		Replace(st::menuIconVideoChat, st::macosMenuIconVideoChat);
		Replace(st::menuIconChatBubble, st::macosMenuIconChatBubble);
		Replace(st::menuIconSavedMessages, st::macosMenuIconSavedMessages);
		Replace(st::menuIconTopics, st::macosMenuIconTopics);
		Replace(st::menuIconChangeColors, st::macosMenuIconChangeColors);
		Replace(st::menuIconExport, st::macosMenuIconExport);
		Replace(st::menuIconShowInChat, st::macosMenuIconShowInChat);
		Replace(st::menuIconTagFilter, st::macosMenuIconTagFilter);
		Replace(st::menuIconTagRename, st::macosMenuIconTagRename);
		Replace(st::menuIconTagRemove, st::macosMenuIconTagRemove);
		Replace(st::menuIconAbove, st::macosMenuIconAbove);
		Replace(st::menuIconBelow, st::macosMenuIconBelow);
		Replace(st::menuIconCaptionShow, st::macosMenuIconCaptionShow);
		Replace(st::menuIconFont, st::macosMenuIconFont);
		Replace(st::infoProfileTopBarActionMessage, st::macosMenuIconChatBubble);
		Replace(st::infoProfileTopBarActionMute, st::macosMenuIconMute);
		Replace(st::infoProfileTopBarActionUnmute, st::macosMenuIconUnmute);
		Replace(st::infoProfileTopBarActionCall, st::macosMenuIconPhone);
		Replace(st::infoProfileTopBarActionSearch, st::macosMenuIconSearch);
		Replace(st::infoProfileTopBarActionLeave, st::macosMenuIconLeave);
		const auto icons = [&](const style::IconButton &button, const style::icon &icon) {
			Replace(button.icon, icon);
			Replace(button.iconOver, icon);
		};
		Replace(st::infoTopBarBackIcon, st::macosTopBarBack);
		Replace(st::infoTopBarBackIconOver, st::macosTopBarBack);
		Replace(st::infoLayerTopBarBackIcon, st::macosTopBarBack);
		Replace(st::infoLayerTopBarBackIconOver, st::macosTopBarBack);
		icons(st::infoTopBarBack, st::macosTopBarBack);
		icons(st::infoLayerTopBarBack, st::macosTopBarBack);
		icons(st::infoTopBarBlackBack, st::macosTopBarBack);
		icons(st::infoLayerTopBarBlackBack, st::macosTopBarBack);
		icons(st::infoTopBarSearch, st::macosTopBarSearch.icon);
		icons(st::infoTopBarBlackSearch, st::macosTopBarSearch.icon);
		icons(st::infoTopBarMenu, st::macosTopBarMenuToggle.icon);
		icons(st::infoTopBarBlackMenu, st::macosTopBarMenuToggle.icon);
		icons(st::infoTopBarCall, st::macosTopBarCall.icon);
		icons(st::infoTopBarEdit, st::macosMenuIconEdit);
		icons(st::infoTopBarBlackEdit, st::macosMenuIconEdit);
		icons(st::infoLayerTopBarEdit, st::macosMenuIconEdit);
		icons(st::infoLayerTopBarBlackEdit, st::macosMenuIconEdit);
		Replace(st::historyAttachEmoji.inner, st::macosHistoryAttachEmojiInner);
		Replace(st::historySend.inner.icon, st::macosHistorySendIcon);
		Replace(st::historySend.inner.iconOver, st::macosHistorySendIconOver);
	}
	if (Enabled) {
		Replace(st::topBarHeight, st::macosTopBarHeight);
		const auto headerButton = [&](const style::IconButton &target) {
			auto replacement = target;
			replacement.height = st::macosTopBarHeight;
			replacement.iconPosition.setY(-1);
			replacement.rippleAreaPosition.setY(style::ConvertScale(5));
			Replace(target, replacement);
		};
		headerButton(st::topBarSearch);
		headerButton(st::topBarCall);
		headerButton(st::topBarMenuToggle);
		headerButton(st::topBarGroupCall);
		headerButton(st::topBarInfo);
		headerButton(st::topBarCloseChoose);
		Replace(st::infoTopBarHeight, st::macosTopBarHeight);
		Replace(st::infoTopBar.height, st::macosTopBarHeight);
		headerButton(st::infoTopBarBack);
		headerButton(st::infoTopBarSearch);
		headerButton(st::infoTopBarMenu);
		headerButton(st::infoTopBarCall);
		headerButton(st::infoTopBarBlackBack);
		headerButton(st::infoTopBarBlackSearch);
		headerButton(st::infoTopBarBlackMenu);
		headerButton(st::infoTopBarBlackEdit);
	}
	if (Enabled || IconsEnabled) {
		const auto syncInfo = [&](const style::InfoTopBar &target, bool layer) {
			auto info = target;
			info.back = layer ? st::infoLayerTopBarBack : st::infoTopBarBack;
			info.search = st::infoTopBarSearch;
			if (IconsEnabled) {
				const auto icon = [&](style::IconButton &button, const style::icon &mask) {
					button.icon = mask;
					button.iconOver = mask;
				};
				icon(info.mediaForward, st::macosMenuIconForward);
				icon(info.mediaDelete, st::macosMenuIconDelete);
				icon(info.storiesArchive, st::macosMenuIconArchive);
				icon(info.storiesPin, st::macosMenuIconPin);
				icon(info.storiesUnpin, st::macosMenuIconUnpin);
			}
			if (Enabled) {
				info.height = st::macosTopBarHeight;
				info.title = st::macosSettingsName;
				info.radius = layer ? st::boxRadius : 0;
				info.back.height = info.height;
				info.search.height = info.height;
			}
			Replace(target, info);
		};
		syncInfo(st::infoTopBar, false);
		syncInfo(st::infoLayerTopBar, true);
		if (Enabled) {
			for (const auto pan : { &st::defaultEmojiPan, &st::statusEmojiPan,
					&st::backgroundEmojiPan, &st::userpicBuilderEmojiPan, &st::reactPanelEmojiPan }) {
				Replace(pan->menu, st::popupMenuWithIcons);
				Replace(pan->menuAttention, st::menuWithIconsAttention);
			}
			auto menu = st::mediaviewMenu;
			menu.itemRadius = st::macosMenuWithIcons.itemRadius;
			menu.itemInset = st::macosMenuWithIcons.itemInset;
			menu.frosted = true;
			menu.itemStyle = st::macosMenuWithIcons.itemStyle;
			menu.itemPadding = st::macosMenuWithIcons.itemPadding;
			Replace(st::mediaviewMenu, menu);
			Replace(st::mediaviewPopupMenu.menu, menu);
			Replace(st::mediaviewPopupMenu.radius, st::macosPopupMenu.radius);
			Replace(st::mediaviewDropdownMenu.menu, menu);
		}
	}

}

bool PaintMacosSettingsIcon(
		QPainter &p,
		const QRect &rect,
		const style::internal::Icon *icon) {
	if (!IconsEnabled) {
		return false;
	}
	struct Entry {
		const style::icon *icon;
		QImage image;
	};
	static const auto images = std::array{
		Entry{ &st::menuIconProfile, QImage(u":/gui/art/jel/macos/profile.png"_q) },
		Entry{ &st::menuIconNotifications, QImage(u":/gui/art/jel/macos/notifications.png"_q) },
		Entry{ &st::menuIconLock, QImage(u":/gui/art/jel/macos/privacy.png"_q) },
		Entry{ &st::menuIconChatBubble, QImage(u":/gui/art/jel/macos/appearance.png"_q) },
		Entry{ &st::menuIconPalette, QImage(u":/gui/art/jel/macos/palette.png"_q) },
		Entry{ &st::menuIconShowInFolder, QImage(u":/gui/art/jel/macos/folders.png"_q) },
		Entry{ &st::menuIconManage, QImage(u":/gui/art/jel/macos/advanced.png"_q) },
		Entry{ &st::menuIconTranslate, QImage(u":/gui/art/jel/macos/language.png"_q) },
	};
	for (const auto &entry : images) {
		if (entry.icon == icon && !entry.image.isNull()) {
			p.save();
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			// Matches generateSettingsIcon in the native macOS client.
			// Its glyph cutouts reveal a white inset in both day and night.
			auto target = QRect(QPoint(), entry.image.size().scaled(
				rect.size(), Qt::KeepAspectRatio));
			target.moveCenter(rect.center());
			p.fillRect(QRectF(target).adjusted(
				target.width() / 8., target.height() / 8.,
				-target.width() / 8., -target.height() / 8.), Qt::white);
			p.drawImage(target, entry.image);
			p.restore();
			return true;
		}
	}
	return false;
}

bool MacosInterfaceEnabled() {
	return Enabled;
}

bool MacosInterfaceIconsEnabled() {
	return IconsEnabled;
}

int MacosTitleControlsInset(not_null<const QWidget*> widget) {
	const auto window = widget->window();
	if (!Enabled || !window->property("jel-macos-unified-title").toBool()) return 0;
	const auto position = widget->mapTo(window, QPoint());
	return position.y() < st::macosTopBarHeight
		? std::max(0, style::ConvertScale(70) - position.x()) : 0;
}

::Ui::RpWidget *CreateMacosNavigation(
		not_null<::Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller) {
	const auto bar = ::Ui::CreateChild<::Ui::RpWidget>(parent);
	const auto sidebar = ::Ui::CreateChild<MacosSidebar>(parent, controller);
	parent->sizeValue() | rpl::on_next([=](QSize size) {
		sidebar->setGeometry(0, 0, size.width(), std::max(0, size.height() - st::macosNavigationHeight));
		sidebar->setVisible(sidebar->currentTab() != 2 && size.width() >= st::columnMinimalWidthLeft);
	}, sidebar->lifetime());
	bar->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(bar);
		p.fillRect(bar->rect(), st::dialogsBg->c);
		p.fillRect(0, 0, bar->width(), st::lineWidth, st::shadowFg->c);
	}, bar->lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		bar->update();
	}, bar->lifetime());
	const auto normal = std::array{
		IconsEnabled ? &st::macosNavigationContacts : &st::menuIconUserShow,
		IconsEnabled ? &st::macosNavigationCalls : &st::menuIconPhone,
		IconsEnabled ? &st::macosNavigationChats : &st::menuIconChatBubble,
		IconsEnabled ? &st::macosNavigationSettings : &st::menuIconSettings,
	};
	const auto active = std::array{
		&st::macosNavigationContactsActive,
		&st::macosNavigationCallsActive,
		&st::macosNavigationChatsActive,
		&st::macosNavigationSettingsActive,
	};
	const auto labels = std::array{
		tr::lng_menu_contacts(tr::now),
		tr::lng_menu_calls(tr::now),
		tr::lng_recent_chats(tr::now),
		tr::lng_menu_settings(tr::now),
	};
	for (auto i = 0; i != int(normal.size()); ++i) {
		const auto button = ::Ui::CreateChild<MacosNavigationButton>(
			bar, st::macosNavigationButton);
		button->setIconOverride(
			(IconsEnabled && i == 2) ? active[i] : normal[i],
			IconsEnabled ? active[i] : normal[i]);
		button->setAccessibleName(labels[i]);
		if (i == 2) {
			const auto badge = ::Ui::CreateChild<::Ui::UnreadBadge>(button);
			badge->setAttribute(Qt::WA_TransparentForMouseEvents);
			const auto data = &controller->session().data();
			rpl::merge(
				rpl::single(rpl::empty),
				data->unreadBadgeChanges(),
				JelSettings::getInstance().hideNotificationCountersValue() | rpl::to_empty
			) | rpl::on_next([=] {
				const auto count = JelSettings::getInstance().hideNotificationCounters() ? 0 : data->unreadBadge();
				badge->setText(count > 99 ? u"99+"_q : QString::number(count), !data->unreadBadgeMuted());
				badge->setVisible(count > 0);
				button->setAccessibleName(count
					? tr::lng_filter_unread_chats(tr::now, lt_count, count, lt_text, labels[i]) : labels[i]);
			}, badge->lifetime());
			rpl::combine(button->sizeValue(), badge->sizeValue()) | rpl::on_next([=] {
				badge->moveToLeft((button->width() - badge->width()) / 2 + style::ConvertScale(10), style::ConvertScale(5));
			}, badge->lifetime());
		}
		button->setIsPageTab(true);
		button->setClickedCallback([=] {
			sidebar->showTab(i);
			bar->raise();
		});
		sidebar->tabChanges() | rpl::on_next([=](int selected) {
			button->setSelected(i == selected);
			button->setIconOverride(IconsEnabled && i == selected ? active[i] : normal[i],
				IconsEnabled ? active[i] : normal[i]);
		}, button->lifetime());
		bar->sizeValue() | rpl::on_next([=](QSize size) {
			const auto left = (size.width() * i) / int(normal.size());
			const auto right = (size.width() * (i + 1)) / int(normal.size());
			button->setGeometry(left, 0, right - left, size.height());
		}, button->lifetime());
		button->show();
	}
	bar->show();
	return bar;
}

namespace {

class MacosSettingsGroup final : public ::Ui::VerticalLayout {
public:
	using VerticalLayout::VerticalLayout;
	void addRow(not_null<::Ui::SettingsButton*> row) {
		_rows.push_back(row.get());
		rpl::combine(row->shownValue(), row->heightValue())
		| rpl::on_next([=] { refresh(); }, lifetime());
		refresh();
	}
private:
	void refresh() {
		auto visible = std::vector<::Ui::SettingsButton*>();
		for (const auto &row : _rows) {
			if (row && !row->isHidden() && row->height() > 0) visible.push_back(row);
		}
		for (auto i = 0; i != int(visible.size()); ++i) {
			visible[i]->setRoundCorners(style::ConvertScale(20), i == 0, i + 1 == int(visible.size()), i + 1 != int(visible.size()));
		}
	}
	std::vector<QPointer<::Ui::SettingsButton>> _rows;

};

} // namespace

void SetupMacosSettingsPage(not_null<::Ui::RpWidget*> page) {
	if (!Enabled) return;
	page->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(page.get()).fillRect(clip, st::boxDividerBg->c);
	}, page->lifetime());
	style::PaletteChanged() | rpl::on_next([=] { page->update(); }, page->lifetime());
}

::Ui::VerticalLayout *CreateMacosSettingsGroup(not_null<::Ui::VerticalLayout*> parent) {
	return parent->add(object_ptr<MacosSettingsGroup>(parent), QMargins(style::ConvertScale(20), 0, style::ConvertScale(20), 0));
}

void AddMacosSettingsRow(not_null<::Ui::VerticalLayout*> group, not_null<::Ui::SettingsButton*> row) {
	static_cast<MacosSettingsGroup*>(group.get())->addRow(row);
}

bool MacosDefaultPaletteEnabled() {
	if (!Enabled || JelFeatures::MessageShot::isChoosingTheme()) return false;
	const auto type = Window::Theme::CurrentEmbeddedType();
	if (!type) return false;
	const auto schemes = Window::Theme::EmbeddedThemes();
	const auto scheme = ranges::find(schemes, *type, &Window::Theme::EmbeddedScheme::type);
	return scheme != end(schemes) && !Window::Theme::ColorizerForTheme(scheme->path);
}

void ApplyMacosPalette() {
	if (!Enabled) {
		return;
	}
	const auto dark = st::windowBg->c.lightness() < 128;
	const auto lens = st::windowFg->c;
	style::main_palette::setColor(QLatin1String("macosTabsSelectionBg"),
		lens.red(), lens.green(), lens.blue(), dark ? 26 : 19);
	if (!MacosDefaultPaletteEnabled()) return;
	auto changed = base::flat_set<QString>();
	const auto set = [&](const char *name, uint32 light, uint32 night) {
		const auto color = QColor::fromRgb(dark ? night : light);
		style::main_palette::setColor(QLatin1String(name),
			color.red(), color.green(), color.blue(), 255);
		changed.emplace(QString::fromLatin1(name));
	};
	// Native whitePalette / systemPalette common surfaces and typography.
	set("activeButtonBg", 0x2481cc, 0x2ea6ff);
	set("activeButtonBgOver", 0x2074b8, 0x52b6ff);
	set("activeButtonBgRipple", 0x1b659e, 0x1588dc);
	set("activeButtonFg", 0xffffff, 0xffffff);
	set("activeLineFg", 0x2481cc, 0x2ea6ff);
	set("activeLineFgError", 0xff3b30, 0xef5b5b);
	set("attentionButtonFg", 0xff3b30, 0xef5b5b);
	set("lightButtonBg", 0xffffff, 0x292a2f);
	set("lightButtonBgOver", 0xf4f4f4, 0x3e464c);
	set("lightButtonBgRipple", 0xe4e4e4, 0x464850);
	set("lightButtonFg", 0x2481cc, 0x2ea6ff);
	set("windowBg", 0xffffff, 0x292a2f);
	set("windowBgOver", 0xf4f4f4, 0x3e464c);
	set("windowBgRipple", 0xe4e4e4, 0x3e464c);
	set("windowFg", 0x000000, 0xffffff);
	set("windowBoldFg", 0x000000, 0xffffff);
	set("windowSubTextFg", 0x999999, 0xb1c3d5);
	set("windowSubTextFgOver", 0x999999, 0xb1c3d5);
	set("windowFgOver", 0x000000, 0xffffff);
	set("windowFgActive", 0xffffff, 0xffffff);
	set("boxBg", 0xffffff, 0x292a2f);
	set("boxDividerBg", 0xf4f4f4, 0x3e464c);
	set("boxTextFg", 0x000000, 0xffffff);
	set("menuBg", 0xffffff, 0x292a2f);
	set("menuBgOver", 0xf4f4f4, 0x3e464c);
	set("menuIconFg", 0x9e9e9e, 0xb1c3d5);
	set("menuIconFgOver", 0x9e9e9e, 0xb1c3d5);
	set("menuSeparatorFg", 0xeaeaea, 0x3d474f);
	set("dialogsNameFg", 0x000000, 0xffffff);
	set("dialogsNameFgOver", 0x000000, 0xffffff);
	set("dialogsTextFg", 0x999999, 0xb1c3d5);
	set("dialogsTextFgOver", 0x999999, 0xb1c3d5);
	set("dialogsDateFg", 0x999999, 0xb1c3d5);
	set("dialogsDateFgOver", 0x999999, 0xb1c3d5);
	set("dialogsUnreadBg", 0x4ba3e2, 0x2ea6ff);
	set("dialogsUnreadBgMuted", 0xd7d7d7, 0xb1c3d5);
	set("dialogsUnreadFg", 0xffffff, 0xffffff);
	set("historyTextInFg", 0x000000, 0xffffff);
	set("historyTextInFgSelected", 0x000000, 0xffffff);
	set("historyComposeAreaFg", 0x000000, 0xffffff);
	set("dialogsBgActive", 0x4c91c7, 0x3b6ba5);
	set("windowBgActive", 0x2481cc, 0x2ea6ff);
	set("windowActiveTextFg", 0x2481cc, 0x2ea6ff);
	set("dialogsBg", 0xffffff, 0x292a2f);
	set("dialogsBgOver", 0xf1f1f1, 0x34353b);
	set("filterInputInactiveBg", 0xf1f1f1, 0x34353b);
	set("filterInputActiveBg", 0xf1f1f1, 0x34353b);
	set("historyComposeAreaBg", 0xffffff, 0x292a2f);
	set("historyComposeIconFg", 0x999999, 0x999999);
	set("historySendIconFg", 0x2481cc, 0x2ea6ff);
	set("historySendIconFgOver", 0x2481cc, 0x2ea6ff);
	set("msgInBg", 0xf7f7fa, 0x4e5058);
	set("msgInBgSelected", 0xccddea, 0x3e6b9b);
	set("msgOutBg", 0x5097cb, 0x416ead);
	set("msgOutBgSelected", 0x6da8d6, 0x375d8a);
	set("historyTextOutFg", 0xffffff, 0xffffff);
	set("historyTextOutFgSelected", 0xffffff, 0xffffff);
	set("historyLinkOutFg", 0xffffff, 0xffffff);
	set("historyLinkOutFgSelected", 0xffffff, 0xffffff);
	set("msgOutServiceFg", 0xeffaff, 0xeffaff);
	set("msgOutServiceFgSelected", 0xeffaff, 0xeffaff);
	set("msgOutDateFg", 0xeffaff, 0xb1c3d5);
	set("msgOutDateFgSelected", 0xeffaff, 0xb1c3d5);
	set("historyOutIconFg", 0xeffaff, 0xb1c3d5);
	set("historyOutIconFgSelected", 0xeffaff, 0xb1c3d5);
	set("msgOutReplyBarColor", 0xeffaff, 0xeffaff);
	set("msgOutReplyBarSelColor", 0xeffaff, 0xeffaff);
	set("msgOutMonoFg", 0xffffff, 0xffffff);
	set("msgOutMonoFgSelected", 0xffffff, 0xffffff);
	const auto rows = style::main_palette::data();
	for (auto grew = true; grew;) {
		grew = false;
		for (const auto &row : rows) {
			const auto name = QString(row.name);
			if (!changed.contains(name) && changed.contains(QString(row.value))) {
				style::main_palette::setColor(row.name, row.value);
				changed.emplace(name);
				grew = true;
			}
		}
	}
}

} // namespace Jel::Ui
