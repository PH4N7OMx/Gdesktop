// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "jel/ui/message_history/history_item.h"

#include "history/history_item.h"
#include "api/api_text_entities.h"
#include "jel/data/entities.h"
#include "jel/ui/message_history/history_inner.h"
#include "jel/utils/jel_mapper.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_file_origin.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "ui/basic_click_handlers.h"
#include "ui/text/text_utilities.h"

#include <array>

namespace MessageHistory {
namespace {

constexpr auto kMessageFlagIsPost = 0x00004000;

[[nodiscard]] PeerData *ResolveSender(
		not_null<History*> history,
		const JelMessageBase &message) {
	const auto storedId = uint64(message.fromId);
	if (storedId & (UserId::kReservedBit << 48)) {
		return history->owner().peerLoaded(DeserializePeerId(storedId));
	}
	if ((message.flags & kMessageFlagIsPost)
		&& history->peer->isChannel()
		&& (history->peer->id.value & PeerId::kChatTypeMask) == storedId) {
		return history->peer;
	}
	const auto candidates = std::array<PeerData*, 3>{
		history->owner().userLoaded(UserId(storedId)),
		history->owner().channelLoaded(ChannelId(storedId)),
		history->owner().chatLoaded(ChatId(storedId)),
	};
	auto result = static_cast<PeerData*>(nullptr);
	for (const auto peer : candidates) {
		if (peer) {
			if (result) {
				return nullptr;
			}
			result = peer;
		}
	}
	return result;
}

} // namespace

OwnedItem::OwnedItem(std::nullptr_t) {
}

OwnedItem::OwnedItem(
	not_null<HistoryView::ElementDelegate*> delegate,
	not_null<HistoryItem*> data)
	: _data(data), _view(_data->createView(delegate)) {
}

OwnedItem::OwnedItem(OwnedItem &&other)
	: _data(base::take(other._data)), _view(base::take(other._view)) {
}

OwnedItem &OwnedItem::operator=(OwnedItem &&other) {
	_data = base::take(other._data);
	_view = base::take(other._view);
	return *this;
}

OwnedItem::~OwnedItem() {
	clearView();
	if (_data) {
		_data->destroy();
	}
}

void OwnedItem::refreshView(
	not_null<HistoryView::ElementDelegate*> delegate) {
	_view = _data->createView(delegate);
}

void OwnedItem::clearView() {
	_view = nullptr;
}

void GenerateItems(
	not_null<HistoryView::ElementDelegate*> delegate,
	not_null<History*> history,
	JelMessageBase message,
	Fn<void(OwnedItem item, TimeId sentDate, MsgId)> callback) {
	const auto from = ResolveSender(history, message);
	const auto date = message.entityCreateDate;
	const auto addPart = [&](
		not_null<HistoryItem*> item,
		TimeId sentDate = 0,
		MsgId realId = MsgId())
	{
		return callback(OwnedItem(delegate, item), sentDate, realId);
	};

	const auto makeSimpleTextMessage = [&](TextWithEntities &&text)
	{
		base::flags<MessageFlag> flags = MessageFlag::AdminLogEntry;
		if (from) {
			flags |= MessageFlag::HasFromId;
		} else {
			flags |= MessageFlag::HasPostAuthor;
		}
		if (!message.postAuthor.empty()) {
			flags |= MessageFlag::HasPostAuthor;
		}

		return history->makeMessage({
										.id = history->nextNonHistoryEntryId(),
										.flags = flags,
										.from = from ? from->id : 0,
										.date = date,
										.postAuthor = !message.postAuthor.empty()
														  ? QString::fromStdString(message.postAuthor)
														  : from
																? QString()
																: QString("unknown user: %1").arg(uint64(message.fromId) & PeerId::kChatTypeMask),
									},
									std::move(text),
									MTP_messageMediaEmpty());
	};

	const auto addSimpleTextMessage = [&](TextWithEntities &&text)
	{
		addPart(makeSimpleTextMessage(std::move(text)));
	};

	const auto text = QString::fromStdString(message.text);
	auto textAndEntities = Ui::Text::WithEntities(text);
	const auto entities = JelMapper::deserializeTextWithEntities(message.textEntities);
	textAndEntities.entities = Api::EntitiesFromMTP(&history->session(), entities.v);
	addSimpleTextMessage(std::move(textAndEntities));
}

} // namespace MessageHistory
