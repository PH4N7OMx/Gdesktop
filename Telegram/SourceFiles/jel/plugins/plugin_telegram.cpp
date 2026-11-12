#include "jel/plugins/plugin_telegram.h"

#include "api/api_common.h"
#include "api/api_sending.h"
#include "base/random.h"
#include "data/data_chat_participant_status.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "main/main_session.h"
#include "apiwrap.h"

#include <QPointer>
#include <QRegularExpression>

namespace JellyPlugins {
namespace {

PeerId PeerFromChatId(const QString &text) {
	auto valid = false;
	const auto value = text.toLongLong(&valid);
	if (!valid || !value) return PeerId();
	const auto bare = uint64(value > 0 ? value : value < -1000000000000LL ? -value - 1000000000000LL : -value);
	if (!bare || bare > PeerId::kChatTypeMask) return PeerId();
	if (value > 0) return peerFromUser(UserId(bare));
	if (value < -1000000000000LL) return peerFromChannel(ChannelId(bare));
	return peerFromChat(ChatId(bare));
}

int SentId(const MTPUpdates &updates, uint64 randomId) {
	if (updates.type() == mtpc_updateShortSentMessage) return updates.c_updateShortSentMessage().vid().v;
	const auto find = [&](const auto &list) {
		for (const auto &update : list) {
			if (update.type() == mtpc_updateMessageID && uint64(update.c_updateMessageID().vrandom_id().v) == randomId) {
				return int(update.c_updateMessageID().vid().v);
			}
		}
		return 0;
	};
	if (updates.type() == mtpc_updates) return find(updates.c_updates().vupdates().v);
	if (updates.type() == mtpc_updatesCombined) return find(updates.c_updatesCombined().vupdates().v);
	return 0;
}

} // namespace

struct TelegramApi::Operation {
	mtpRequestId requestId = 0;
	uint64 randomId = 0;
	FullMsgId localId = {};
	bool canceled = false;
};

TelegramApi::TelegramApi(not_null<Main::Session*> session) : _session(session) {
}

TelegramApi::~TelegramApi() {
	while (!_operations.empty()) cancel(_operations.begin()->first);
}

QString TelegramApi::recipient(const Client::Action &action) const {
	const auto id = action.params.value(u"chatId"_q).toString();
	const auto peer = _session->data().peerLoaded(PeerFromChatId(id));
	return peer ? peer->name() + u" ("_q + id + ')' : id;
}

void TelegramApi::cleanup(const std::shared_ptr<Operation> &operation, bool removeLocal) {
	if (!operation->randomId) return;
	_session->data().unregisterMessageRandomId(operation->randomId);
	_session->data().unregisterMessageSentData(operation->randomId);
	if (removeLocal) {
		if (const auto message = _session->data().message(operation->localId)) message->destroy();
	}
	operation->randomId = 0;
}

void TelegramApi::cancel(const QString &token) {
	const auto i = _operations.find(token);
	if (i == _operations.end()) return;
	const auto operation = i->second;
	operation->canceled = true;
	if (operation->requestId) _session->api().request(operation->requestId).cancel();
	cleanup(operation, true);
	_operations.erase(i);
}

void TelegramApi::execute(const QString &token, const Client::Action &request, std::function<void(TelegramResult)> complete) {
	const auto peer = _session->data().peerLoaded(PeerFromChatId(request.params.value(u"chatId"_q).toString()));
	if (!peer || _operations.contains(token)) { complete({ {}, u"CHAT_UNAVAILABLE"_q }); return; }
	const auto operation = std::make_shared<Operation>();
	_operations[token] = operation;
	const auto guard = QPointer<TelegramApi>(this);
	const auto finish = [=](TelegramResult result, bool removeLocal) {
		if (!guard || operation->canceled) return;
		cleanup(operation, removeLocal);
		_operations.erase(token);
		complete(std::move(result));
	};
	const auto failed = [=](const MTP::Error &error) {
		static const auto pattern = QRegularExpression(u"^FLOOD(?:_PREMIUM)?_WAIT_([0-9]+)$"_q);
		const auto match = pattern.match(error.type());
		const auto retry = match.hasMatch() ? match.captured(1).toInt() : 0;
		const auto code = retry ? u"FLOOD_WAIT"_q
			: request.method == u"telegram.sendText"_q && (error.code() < 0 || error.code() >= 500) ? u"OUTCOME_UNKNOWN"_q : error.type();
		finish({ {}, code, retry }, true);
	};
	if (request.method == u"telegram.sendText"_q) {
		if (!Data::CanSendTexts(peer) || peer->starsPerMessageChecked() != 0) { finish({ {}, u"CHAT_NOT_WRITABLE"_q }, false); return; }
		const auto text = request.params.value(u"text"_q).toString();
		auto action = Api::SendAction(_session->data().history(peer));
		using Flag = MTPmessages_SendMessage::Flag;
		auto flags = MTPmessages_SendMessage::Flags(Flag::f_no_webpage);
		if (request.params.value(u"silent"_q).toBool()) flags |= Flag::f_silent;
		operation->randomId = base::RandomValue<uint64>();
		operation->localId = FullMsgId(peer->id, _session->data().nextLocalMessageId());
		auto localFlags = NewMessageFlags(peer);
		Api::FillMessagePostFlags(action, peer, localFlags);
		const auto local = action.history->addNewLocalMessage({
			.id = operation->localId.msg,
			.flags = localFlags,
			.from = NewMessageFromId(action),
			.replyTo = action.replyTo,
			.date = NewMessageDate(action.options),
			.postAuthor = NewMessagePostAuthor(action),
		}, TextWithEntities{ text }, MTP_messageMediaEmpty());
		_session->data().registerMessageRandomId(operation->randomId, local->fullId());
		_session->data().registerMessageSentData(operation->randomId, peer->id, text);
		operation->requestId = _session->api().request(MTPmessages_SendMessage(
			MTP_flags(flags), peer->input(), action.mtpReplyTo(), MTP_string(text), MTP_long(operation->randomId),
			MTPReplyMarkup(), MTPVector<MTPMessageEntity>(), MTP_int(0), MTP_int(0), MTP_inputPeerEmpty(),
			MTPInputQuickReplyShortcut(), MTP_long(0), MTP_long(0), MTPSuggestedPost(), MTPInputRichMessage()
		)).done([=](const MTPUpdates &updates) {
			if (!guard || operation->canceled) return;
			const auto id = SentId(updates, operation->randomId);
			_session->api().applyUpdates(updates, operation->randomId);
			if (!id) finish({ {}, u"OUTCOME_UNKNOWN"_q }, false);
			else finish({ QJsonObject{ { u"messageId"_q, QString::number(id) } } }, false);
		}).fail(failed).handleAllErrors().send();
	} else if (request.method == u"telegram.readHistory"_q) {
		const auto limit = request.params.value(u"limit"_q).toInt();
		operation->requestId = _session->api().request(MTPmessages_GetHistory(
			peer->input(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(limit), MTP_int(0), MTP_int(0), MTP_long(0)
		)).done([=](const MTPmessages_Messages &result) {
			if (!guard || operation->canceled) return;
			auto messages = QJsonArray();
			const auto collect = [&](const auto &data) {
				_session->data().processUsers(data.vusers());
				_session->data().processChats(data.vchats());
				for (const auto &message : data.vmessages().v) {
					if (messages.size() >= limit) break;
					const auto loaded = _session->data().addNewMessage(message, MessageFlags(), NewMessageType::Existing);
					if (!loaded || loaded->isService() || loaded->history()->peer != peer) continue;
					messages.push_back(QJsonObject{ { u"id"_q, QString::number(loaded->id.bare) }, { u"text"_q, loaded->originalText().text } });
				}
			};
			result.match([&](const MTPDmessages_messages &data) { collect(data); },
				[&](const MTPDmessages_messagesSlice &data) { collect(data); },
				[&](const MTPDmessages_channelMessages &data) { collect(data); },
				[&](const MTPDmessages_messagesNotModified &) {});
			finish({ messages }, false);
		}).fail(failed).handleAllErrors().send();
	} else {
		finish({ {}, u"METHOD_UNAVAILABLE"_q }, false);
	}
}

} // namespace JellyPlugins
