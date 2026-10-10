// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "jel/ui/settings/settings_chats.h"

#include "lang_auto.h"
#include "jel/jel_settings.h"
#include "jel/ui/boxes/edit_mark_box.h"
#include "jel/ui/components/message_preview.h"
#include "jel/ui/settings/jel_builder.h"
#include "jel/ui/settings/settings_jel_utils.h"
#include "jel/ui/settings/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_jel_icons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <memory>

namespace Settings {

using namespace Builder;
using namespace JelBuilder;

namespace {

struct PreviewState {
	MessagePreview *widget = nullptr;
};

void BuildStickersAndEmoji(SectionBuilder &builder, JelSectionBuilder &jel) {
	builder.addSubsectionTitle(tr::lng_settings_stickers_emoji());

	jel.addSettingToggle({
		.id = u"jel/showOnlyAddedEmojisAndStickers"_q,
		.title = tr::jel_ShowOnlyAddedEmojisAndStickers(),
		.getter = &JelSettings::showOnlyAddedEmojisAndStickers,
		.setter = &JelSettings::setShowOnlyAddedEmojisAndStickers,
	});

	jel.addSettingToggle({
		.id = u"jel/unlimitedRecentStickers"_q,
		.altIds = { u"jel/recentStickersCount"_q },
		.title = tr::jel_SettingsUnlimitedRecentStickers(),
		.getter = &JelSettings::unlimitedRecentStickers,
		.setter = &JelSettings::setUnlimitedRecentStickers,
	});

	jel.addCollapsibleToggle({
		.id = u"jel/hideReactions"_q,
		.title = tr::jel_HideReactions(),
		.checkboxes = {
			NestedEntry{
				tr::jel_HideReactionsInChannels(),
				[] { return !JelSettings::getInstance().showChannelReactions(); },
				[](bool v) { JelSettings::getInstance().setShowChannelReactions(!v); }
			},
			NestedEntry{
				tr::jel_HideReactionsInGroups(),
				[] { return !JelSettings::getInstance().showGroupReactions(); },
				[](bool v) { JelSettings::getInstance().setShowGroupReactions(!v); }
			},
			NestedEntry{
				tr::jel_HideReactionsInPrivateChats(),
				[] { return !JelSettings::getInstance().showPrivateChatReactions(); },
				[](bool v) { JelSettings::getInstance().setShowPrivateChatReactions(!v); }
			}
		},
		.toggledWhenAll = false,
	});

	jel.addSectionDivider();
}

void BuildGroupsAndChannels(SectionBuilder &builder, JelSectionBuilder &jel) {
	auto *settings = &JelSettings::getInstance();

	builder.addSubsectionTitle(tr::lng_premium_double_limits_subtitle_channels());

	jel.addChooseButton({
		.id = u"jel/channelBottomButton"_q,
		.altIds = { u"jel/bottomButton"_q },
		.title = tr::jel_ChannelBottomButton(),
		.boxTitle = tr::jel_ChannelBottomButton(),
		.initialSelection = static_cast<int>(settings->channelBottomButton()),
		.options = {
			tr::jel_ChannelBottomButtonHide(tr::now),
			tr::jel_ChannelBottomButtonMute(tr::now),
			tr::jel_ChannelBottomButtonDiscuss(tr::now),
		},
		.setter = [](int index) {
			JelSettings::getInstance().setChannelBottomButton(
				static_cast<ChannelBottomButton>(index));
		},
	});

	jel.addSettingToggle({
		.id = u"jel/quickAdminShortcuts"_q,
		.title = tr::jel_QuickAdminShortcuts(),
		.getter = &JelSettings::quickAdminShortcuts,
		.setter = &JelSettings::setQuickAdminShortcuts,
	});
	jel.addSettingToggle({
		.id = u"jel/disableGreetingSticker"_q,
		.title = tr::jel_DisableGreetingSticker(),
		.getter = &JelSettings::disableGreetingSticker,
		.setter = &JelSettings::setDisableGreetingSticker,
	});
	jel.addSettingToggle({
		.id = u"jel/showMessageShot"_q,
		.title = tr::jel_SettingsShowMessageShot(),
		.getter = &JelSettings::showMessageShot,
		.setter = &JelSettings::setShowMessageShot,
	});

	builder.addSkip();
	builder.addDividerText(tr::jel_SettingsShowMessageShotDescription());
	builder.addSkip();
}

void BuildMarks(
		SectionBuilder &builder,
		JelSectionBuilder &jel,
		std::shared_ptr<PreviewState> previewState) {
	auto *settings = &JelSettings::getInstance();
	const auto controller = builder.controller();

	builder.addSubsectionTitle(tr::lng_settings_messages());

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<MessagePreview>(ctx.container, controller);
		previewState->widget = preview.data();
		return {
			.widget = std::move(preview),
			.margin = style::margins(
				0,
				st::defaultVerticalListSkip,
				0,
				st::settingsPrivacySkipTop),
		};
	});

	jel.addSettingToggle({
		.id = u"jel/replaceBottomInfoWithIcons"_q,
		.altIds = { u"jel/replaceEditedWithIcon"_q },
		.title = tr::jel_ReplaceMarksWithIcons(),
		.getter = &JelSettings::replaceBottomInfoWithIcons,
		.setter = &JelSettings::setReplaceBottomInfoWithIcons,
	});

	builder.scope([&] {
		builder.addButton({
			.id = u"jel/deletedMark"_q,
			.title = tr::jel_DeletedMarkText(),
			.st = &st::settingsButtonNoIcon,
			.label = JelSettings::getInstance().deletedMarkValue(),
			.onClick = [=] {
				auto box = Box<EditMarkBox>(
					tr::jel_DeletedMarkText(),
					settings->deletedMark(),
					QString("🧹"),
					[=](const QString &value) {
						JelSettings::getInstance().setDeletedMark(value);
					});
				Ui::show(std::move(box));
			},
		});

		builder.addButton({
			.id = u"jel/editedMark"_q,
			.title = tr::jel_EditedMarkText(),
			.st = &st::settingsButtonNoIcon,
			.label = JelSettings::getInstance().editedMarkValue(),
			.onClick = [=] {
				auto box = Box<EditMarkBox>(
					tr::jel_EditedMarkText(),
					settings->editedMark(),
					tr::lng_edited(tr::now),
					[=](const QString &value) {
						JelSettings::getInstance().setEditedMark(value);
					});
				Ui::show(std::move(box));
			},
		});
	}, JelSettings::getInstance().replaceBottomInfoWithIconsValue()
		| rpl::map([](bool v) { return !v; }));

	jel.addSettingToggle({
		.id = u"jel/removeMessageTail"_q,
		.title = tr::jel_RemoveMessageTail(),
		.getter = &JelSettings::removeMessageTail,
		.setter = &JelSettings::setRemoveMessageTail,
	});

	jel.addSettingToggle({
		.id = u"jel/hideFastShare"_q,
		.altIds = { u"jel/hideShareButton"_q },
		.title = tr::jel_HideShareButton(),
		.getter = &JelSettings::hideFastShare,
		.setter = &JelSettings::setHideFastShare,
	});
	jel.addSettingToggle({
		.id = u"jel/simpleQuotesAndReplies"_q,
		.altIds = { u"jel/disableColorfulReplies"_q, u"jel/replyElements"_q },
		.title = tr::jel_SimpleQuotesAndReplies(),
		.getter = &JelSettings::simpleQuotesAndReplies,
		.setter = &JelSettings::setSimpleQuotesAndReplies,
	});

	const auto semiTransparent = jel.addSettingToggle({
		.id = u"jel/semiTransparentDeletedMessages"_q,
		.altIds = { u"jel/translucentDeletedMessages"_q },
		.title = tr::jel_SemiTransparentDeletedMessages(),
		.getter = &JelSettings::semiTransparentDeletedMessages,
		.setter = &JelSettings::setSemiTransparentDeletedMessages,
	});
	if (semiTransparent) {
		jel.addBetaBadge(semiTransparent);
	}

	jel.addSectionDivider();
}

void BuildWideMessagesMultiplier(
		SectionBuilder &builder,
		JelSectionBuilder &jel,
		std::shared_ptr<PreviewState> previewState) {
	auto *settings = &JelSettings::getInstance();

	constexpr auto kMinSize = 1.00;
	constexpr auto kStep = 0.05;

	const auto valueToIndex = [=](double value) {
		return static_cast<int>(std::round((value - kMinSize) / kStep));
	};

	const auto controller = builder.controller();
	jel.addSlider({
		.id = u"jel/messageBubbleRadius"_q,
		.title = tr::jel_MessageBubbleRadius(),
		.steps = 17,
		.current = settings->messageBubbleRadius(),
		.indexToValue = [](int index) { return index; },
		.onChanged = [=](int index) {
			if (previewState->widget) {
				previewState->widget->setBubbleRadius(index);
			}
		},
		.onFinalChanged = [=](int index) {
			if (previewState->widget) {
				previewState->widget->setBubbleRadius(index);
			}
			JelSettings::getInstance().setMessageBubbleRadius(index);
			ShowRestartPrompt(controller);
		},
		.formatLabel = [](int index) {
			return QString::number(index);
		},
	});

	jel.addSectionDivider();

	jel.addSlider({
		.id = u"jel/wideMultiplier"_q,
		.title = tr::jel_SettingsWideMultiplier(),
		.steps = 61, // (4.00 - 1.00) / 0.05 + 1
		.current = valueToIndex(settings->wideMultiplier()),
		.indexToValue = [](int index) { return index; },
		.onChanged = nullptr,
		.onFinalChanged = [=](int index) {
			JelSettings::getInstance().setWideMultiplier(
				kMinSize + index * kStep);
			ShowRestartPrompt(controller);
		},
		.formatLabel = [=](int index) {
			return QString::number(kMinSize + index * kStep, 'f', 2);
		},
	});

	builder.addSkip();
	builder.addDividerText(tr::jel_SettingsWideMultiplierDescription());
	builder.addSkip();
}

void BuildContextMenuElements(SectionBuilder &builder, JelSectionBuilder &jel) {
	auto *settings = &JelSettings::getInstance();

	builder.addSubsectionTitle(tr::jel_ContextMenuElementsHeader());

	const auto options = std::vector{
		tr::jel_SettingsContextMenuItemHidden(tr::now),
		tr::jel_SettingsContextMenuItemShown(tr::now),
		tr::jel_SettingsContextMenuItemExtended(tr::now),
	};

	jel.addChooseButton({
		.id = u"jel/showReactionsPanelInContextMenu"_q,
		.title = tr::jel_SettingsContextMenuReactionsPanel(),
		.boxTitle = tr::jel_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showReactionsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { JelSettings::getInstance().setShowReactionsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconReactions },
	});
	jel.addChooseButton({
		.id = u"jel/showViewsPanelInContextMenu"_q,
		.title = tr::jel_SettingsContextMenuViewsPanel(),
		.boxTitle = tr::jel_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showViewsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { JelSettings::getInstance().setShowViewsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconShowInChat },
	});
	jel.addChooseButton({
		.id = u"jel/showHideMessageInContextMenu"_q,
		.title = tr::jel_ContextHideMessage(),
		.boxTitle = tr::jel_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showHideMessageInContextMenu()),
		.options = options,
		.setter = [](int i) { JelSettings::getInstance().setShowHideMessageInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconClear },
	});
	jel.addChooseButton({
		.id = u"jel/showUserMessagesInContextMenu"_q,
		.title = tr::jel_UserMessagesMenuText(),
		.boxTitle = tr::jel_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showUserMessagesInContextMenu()),
		.options = options,
		.setter = [](int i) { JelSettings::getInstance().setShowUserMessagesInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconTTL },
	});
	jel.addChooseButton({
		.id = u"jel/showMessageDetailsInContextMenu"_q,
		.title = tr::jel_MessageDetailsPC(),
		.boxTitle = tr::jel_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showMessageDetailsInContextMenu()),
		.options = options,
		.setter = [](int i) { JelSettings::getInstance().setShowMessageDetailsInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconInfo },
	});
	jel.addChooseButton({
		.id = u"jel/showRepeatMessageInContextMenu"_q,
		.title = tr::jel_RepeatMessage(),
		.boxTitle = tr::jel_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showRepeatMessageInContextMenu()),
		.options = options,
		.setter = [](int i) { JelSettings::getInstance().setShowRepeatMessageInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::jelRepeatMenuIcon },
	});
	if (settings->filtersEnabled()) {
		jel.addChooseButton({
			.id = u"jel/showAddFilterInContextMenu"_q,
			.title = tr::jel_RegexFilterQuickAdd(),
			.boxTitle = tr::jel_SettingsContextMenuTitle(),
			.initialSelection = static_cast<int>(settings->showAddFilterInContextMenu()),
			.options = options,
			.setter = [](int i) { JelSettings::getInstance().setShowAddFilterInContextMenu(static_cast<ContextMenuVisibility>(i)); },
			.icon = { &st::menuIconAddToFolder },
		});
	}

	builder.addSkip();
	builder.addDividerText(tr::jel_SettingsContextMenuDescription());
	builder.addSkip();
}

void BuildMessageFieldElements(SectionBuilder &builder, JelSectionBuilder &jel) {
	builder.addSubsectionTitle(tr::jel_MessageFieldElementsHeader());

	jel.addSettingToggle({
		.id = u"jel/showAttachButtonInMessageField"_q,
		.title = tr::jel_MessageFieldElementAttach(),
		.getter = &JelSettings::showAttachButtonInMessageField,
		.setter = &JelSettings::setShowAttachButtonInMessageField,
		.icon = { &st::messageFieldAttachIcon },
	});
	jel.addSettingToggle({
		.id = u"jel/showCommandsButtonInMessageField"_q,
		.title = tr::jel_MessageFieldElementCommands(),
		.getter = &JelSettings::showCommandsButtonInMessageField,
		.setter = &JelSettings::setShowCommandsButtonInMessageField,
		.icon = { &st::messageFieldCommandsIcon },
	});
	jel.addSettingToggle({
		.id = u"jel/showAutoDeleteButtonInMessageField"_q,
		.title = tr::jel_MessageFieldElementTTL(),
		.getter = &JelSettings::showAutoDeleteButtonInMessageField,
		.setter = &JelSettings::setShowAutoDeleteButtonInMessageField,
		.icon = { &st::messageFieldTTLIcon },
	});
	jel.addSettingToggle({
		.id = u"jel/showEmojiButtonInMessageField"_q,
		.title = tr::jel_MessageFieldElementEmoji(),
		.getter = &JelSettings::showEmojiButtonInMessageField,
		.setter = &JelSettings::setShowEmojiButtonInMessageField,
		.icon = { &st::messageFieldEmojiIcon },
	});
	jel.addSettingToggle({
		.id = u"jel/showMicrophoneButtonInMessageField"_q,
		.title = tr::jel_MessageFieldElementVoice(),
		.getter = &JelSettings::showMicrophoneButtonInMessageField,
		.setter = &JelSettings::setShowMicrophoneButtonInMessageField,
		.icon = { &st::messageFieldVoiceIcon },
	});
	jel.addSettingToggle({
		.id = u"jel/showGiftButtonInMessageField"_q,
		.title = tr::lng_profile_action_short_gift(),
		.getter = &JelSettings::showGiftButtonInMessageField,
		.setter = &JelSettings::setShowGiftButtonInMessageField,
		.icon = { &st::settingsButtonIconGift },
	});
	jel.addSettingToggle({
		.id = u"jel/showAiEditorButtonInMessageField"_q,
		.title = tr::lng_ai_compose_title(),
		.getter = &JelSettings::showAiEditorButtonInMessageField,
		.setter = &JelSettings::setShowAiEditorButtonInMessageField,
		.icon = { &st::messageFieldCocoonAiIcon },
	});

	jel.addSectionDivider();
}

void BuildMessageFieldPopups(SectionBuilder &builder, JelSectionBuilder &jel) {
	builder.addSubsectionTitle(tr::jel_MessageFieldPopupsHeader());

	jel.addSettingToggle({
		.id = u"jel/showAttachPopup"_q,
		.title = tr::jel_MessageFieldElementAttach(),
		.getter = &JelSettings::showAttachPopup,
		.setter = &JelSettings::setShowAttachPopup,
		.icon = { &st::messageFieldAttachIcon },
	});
	jel.addSettingToggle({
		.id = u"jel/showEmojiPopup"_q,
		.title = tr::jel_MessageFieldElementEmoji(),
		.getter = &JelSettings::showEmojiPopup,
		.setter = &JelSettings::setShowEmojiPopup,
		.icon = { &st::messageFieldEmojiIcon },
	});
}

const auto kMeta = BuildHelper({
	.id = JelChats::Id(),
	.parentId = JelMain::Id(),
	.title = &tr::jel_CategoryChats,
	.icon = &st::menuIconChatBubble,
}, [](SectionBuilder &builder) {
	auto jel = JelSectionBuilder(builder);
	const auto previewState = std::make_shared<PreviewState>();

	builder.addSkip();
	BuildStickersAndEmoji(builder, jel);
	BuildGroupsAndChannels(builder, jel);
	BuildMarks(builder, jel, previewState);
	BuildWideMessagesMultiplier(builder, jel, previewState);
	BuildContextMenuElements(builder, jel);
	BuildMessageFieldElements(builder, jel);
	BuildMessageFieldPopups(builder, jel);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> JelChats::title() {
	return tr::jel_CategoryChats();
}

JelChats::JelChats(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void JelChats::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type JelChatsId() {
	return JelChats::Id();
}

} // namespace Settings
