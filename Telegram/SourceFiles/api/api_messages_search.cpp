/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_message_reaction_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "jel/features/search/search_filters.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kSearchPerPage = 50;

[[nodiscard]] MessageIdsList HistoryItemsFromTL(
		not_null<Data::Session*> data,
		const QVector<MTPMessage> &messages) {
	auto result = MessageIdsList();
	for (const auto &message : messages) {
		const auto peerId = PeerFromMessage(message);
		if (data->peerLoaded(peerId)) {
			if (DateFromMessage(message)) {
				const auto item = data->addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);
				result.push_back(item->fullId());
			}
		} else {
			LOG(("API Error: a search results with not loaded peer %1"
				).arg(peerId.value));
		}
	}
	return result;
}

[[nodiscard]] QString RequestToToken(
		const MessagesSearch::Request &request) {
	auto result = request.query;
	if (request.from) {
		result += '\n' + QString::number(request.from->id.value);
	}
	for (const auto &tag : request.tags) {
		result += '\n';
		if (const auto customId = tag.custom()) {
			result += u"custom"_q + QString::number(customId);
		} else {
			result += u"emoji"_q + tag.emoji();
		}
	}
	switch (request.filter) {
	case SearchFilter::NoFilter: break;
	case SearchFilter::Pinned: result += u"\npinned"_q; break;
	}
	result += Jel::SearchExtraFiltersToken(request.extraFilters);
	return result;
}

[[nodiscard]] MTPMessagesFilter PrepareFilter(
		const MessagesSearch::Request &request) {
	if (request.filter == SearchFilter::Pinned) {
		return MTP_inputMessagesFilterPinned();
	}
	return Jel::SearchMediaFilterToMTP(request.extraFilters.mediaFilter);
}

} // namespace

MessagesSearch::MessagesSearch(not_null<History*> history)
: _history(history) {
}

MessagesSearch::~MessagesSearch() {
	_history->owner().histories().cancelRequest(
		base::take(_searchInHistoryRequest));
}

void MessagesSearch::searchMessages(Request request) {
	_history->owner().histories().cancelRequest(
		base::take(_searchInHistoryRequest));
	_requestId = 0;
	_request = std::move(request);
	_offsetId = {};
	_loadedCount = 0;
	_emptyClientPages = 0;
	_exhausted = false;
	searchRequest();
}

void MessagesSearch::searchMore() {
	if (_searchInHistoryRequest || _requestId || _exhausted) {
		return;
	}
	searchRequest();
}

void MessagesSearch::searchRequest() {
	const auto nextToken = RequestToToken(_request);
	if (!_offsetId && _request.extraFilters.isDefault()) {
		const auto it = _cacheOfStartByToken.find(nextToken);
		if (it != end(_cacheOfStartByToken)) {
			_requestId = 0;
			searchReceived(it->second, _requestId, nextToken);
			return;
		}
	}
	auto callback = [=](Fn<void()> finish) {
		using Flag = MTPmessages_Search::Flag;
		const auto from = _request.from;
		const auto fromPeer = _history->peer->isUser() ? nullptr : from;
		const auto savedPeer = _history->peer->isSelf() ? from : nullptr;
		const auto bounds = Jel::ResolveSearchDateBounds(_request.extraFilters);
		const auto mtpMinDate = (bounds.minDate > 0)
			? (bounds.minDate - 1)
			: 0;
		const auto pagination = Jel::ComputeSearchPagination(
			_request.extraFilters.sortOrder,
			_offsetId.bare,
			kSearchPerPage);
		_requestId = _history->session().api().request(MTPmessages_Search(
			MTP_flags((fromPeer ? Flag::f_from_id : Flag())
				| (savedPeer ? Flag::f_saved_peer_id : Flag())
				| (_request.topMsgId ? Flag::f_top_msg_id : Flag())
				| (_request.tags.empty() ? Flag() : Flag::f_saved_reaction)),
			_history->peer->input(),
			MTP_string(_request.query),
			(fromPeer ? fromPeer->input() : MTP_inputPeerEmpty()),
			(savedPeer ? savedPeer->input() : MTP_inputPeerEmpty()),
			MTP_vector_from_range(_request.tags | ranges::views::transform(
				Data::ReactionToMTP
			)),
			MTP_int(_request.topMsgId), // top_msg_id
			PrepareFilter(_request),
			MTP_int(mtpMinDate), // min_date
			MTP_int(bounds.maxDate), // max_date
			MTP_int(pagination.offsetId), // offset_id
			MTP_int(pagination.addOffset), // add_offset
			MTP_int(kSearchPerPage),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done([=](const TLMessages &result, mtpRequestId id) {
			_searchInHistoryRequest = 0;
			searchReceived(result, id, nextToken);
			finish();
		}).fail([=](const MTP::Error &error, mtpRequestId id) {
			_searchInHistoryRequest = 0;

			if (_requestId == id) {
				_requestId = 0;
			}
			if (error.type() == u"SEARCH_QUERY_EMPTY"_q) {
				_exhausted = true;
				_messagesFounds.fire({ 0, MessageIdsList(), nextToken });
			}

			finish();
		}).send();
		return _requestId;
	};
	_searchInHistoryRequest = _history->owner().histories().sendRequest(
		_history,
		Data::Histories::RequestType::History,
		std::move(callback));
}

void MessagesSearch::searchReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken) {
	if (requestId != _requestId) {
		return;
	}
	auto &owner = _history->owner();
	const auto oldestFirst = _request.extraFilters.isOldestFirst();
	const auto bounds = Jel::ResolveSearchDateBounds(_request.extraFilters);
	const auto applyClientFilter = _request.extraFilters.hasClientSideSenderFilter()
		|| !bounds.empty();
	const auto prevOffsetId = _offsetId;
	auto rawCount = 0;
	auto maxRawMsgId = MsgId(0);
	auto minRawMsgId = MsgId(0);
	auto serverExhausted = false;

	const auto processItems = [&](const QVector<MTPMessage> &messages) {
		rawCount = int(messages.size());
		for (const auto &message : messages) {
			const auto msgId = IdFromMessage(message);
			if (msgId > maxRawMsgId) {
				maxRawMsgId = msgId;
			}
			minRawMsgId = msgId;
		}
		auto items = HistoryItemsFromTL(&owner, messages);
		if (applyClientFilter) {
			auto filtered = MessageIdsList();
			filtered.reserve(items.size());
			for (const auto &id : items) {
				if (const auto item = owner.message(id)) {
					if (Jel::MatchesClientSideFilters(
							_request.extraFilters,
							bounds,
							item->out(),
							item->date())) {
						filtered.push_back(id);
					}
				}
			}
			items = std::move(filtered);
		}
		if (oldestFirst) {
			std::reverse(items.begin(), items.end());
		}
		return items;
	};

	auto found = result.match([&](const MTPDmessages_messages &data) {
		if (_requestId != 0) {
			// Don't apply cached data!
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
			_history->peer->processTopics(data.vtopics());
		}
		serverExhausted = true;
		auto items = processItems(data.vmessages().v);
		const auto total = int(items.size());
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_messagesSlice &data) {
		if (_requestId != 0) {
			// Don't apply cached data!
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
			_history->peer->processTopics(data.vtopics());
		}
		auto items = processItems(data.vmessages().v);
		// data.vnext_rate() is used only in global search.
		const auto total = int(data.vcount().v);
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_channelMessages &data) {
		if (_requestId != 0) {
			// Don't apply cached data!
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
			if (const auto channel = _history->peer->asChannel()) {
				channel->ptsReceived(data.vpts().v);
			} else {
				LOG(("API Error: "
					"received messages.channelMessages when no channel "
					"was passed!"));
			}
			_history->peer->processTopics(data.vtopics());
		}
		auto items = processItems(data.vmessages().v);
		const auto total = int(data.vcount().v);
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_messagesNotModified &data) {
		serverExhausted = true;
		return FoundMessages{};
	});
	if (!_offsetId && _request.extraFilters.isDefault()) {
		_cacheOfStartByToken.emplace(nextToken, result);
	}
	_requestId = 0;
	if (rawCount == 0
		|| serverExhausted
		|| (oldestFirst && prevOffsetId > 0 && maxRawMsgId <= prevOffsetId)) {
		_exhausted = true;
	}
	if (oldestFirst) {
		if (maxRawMsgId > 0) {
			_offsetId = maxRawMsgId;
		}
	} else {
		_offsetId = minRawMsgId;
	}
	_loadedCount += int(found.messages.size());
	if (_request.extraFilters.hasClientSideSenderFilter()) {
		found.total = _exhausted
			? _loadedCount
			: std::max(_loadedCount + 1, found.total);
	} else if (_exhausted && found.total > _loadedCount) {
		found.total = _loadedCount;
	}
	if (applyClientFilter
		&& found.messages.empty()
		&& rawCount > 0
		&& !_exhausted
		&& _emptyClientPages < 6) {
		++_emptyClientPages;
		searchRequest();
		return;
	}
	if (!found.messages.empty()) {
		_emptyClientPages = 0;
	}
	_messagesFounds.fire(std::move(found));
}

rpl::producer<FoundMessages> MessagesSearch::messagesFounds() const {
	return _messagesFounds.events();
}

} // namespace Api
