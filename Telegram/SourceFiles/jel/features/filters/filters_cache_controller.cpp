// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "jel/features/filters/filters_cache_controller.h"

#include "jel/data/jel_database.h"
#include "jel/features/filters/filters_controller.h"
#include "data/data_groups.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "rpl/event_stream.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_set>

namespace FiltersCacheController {

std::mutex rebuildMutex;
std::mutex cacheMutex;
std::mutex filteredMessagesMutex;

rpl::event_stream<> filtersUpdateStream;

void fireUpdate() {
	filtersUpdateStream.fire({});
}

rpl::producer<> updates() {
	return filtersUpdateStream.events();
}

std::shared_ptr<const Cache> cache;

using DialogKey = std::pair<uint64, uint64>;

std::map<DialogKey, std::unordered_map<int64, bool>> filteredMessages;
std::set<DialogKey> dialogsWithHiddenBlockedMessages; // purely for show / hide filtered messages

DialogKey dialogKey(not_null<PeerData*> peer) {
	return { peer->session().uniqueId(), peer->id.value };
}

void clearSession(uint64 sessionId) {
	std::lock_guard lock(filteredMessagesMutex);
	std::erase_if(filteredMessages, [=](const auto &entry) {
		return entry.first.first == sessionId;
	});
	std::erase_if(dialogsWithHiddenBlockedMessages, [=](const DialogKey &key) {
		return key.first == sessionId;
	});
}

std::shared_ptr<const Cache> buildCache() {
	const auto filters = JelDatabase::getAllRegexFilters();
	const auto exclusions = JelDatabase::getAllFiltersExclusions();

	std::vector<HashablePattern> shared;
	std::unordered_map<ID, std::vector<ReversiblePattern>> byDialogId;

	for (const auto &filter : filters) {
		if (!filter.enabled || filter.text.empty()) {
			continue;
		}

		int flags = UREGEX_MULTILINE;
		if (filter.caseInsensitive) flags |= UREGEX_CASE_INSENSITIVE;

		auto status = U_ZERO_ERROR;
		auto pattern = icu::RegexPattern::compile(icu::UnicodeString::fromUTF8(filter.text), flags, status);

		if (!pattern) {
			continue;
		}

		if (filter.dialogId.has_value()) {
			byDialogId[filter.dialogId.value()].push_back({
				std::shared_ptr<icu::RegexPattern>(pattern),
				filter.reversed,
			});
		} else {
			shared.push_back({
				filter.id,
				{
					std::shared_ptr<icu::RegexPattern>(pattern),
					filter.reversed,
				},
			});
		}
	}

	auto exclByDialogId = buildExclusions(exclusions, shared);
	auto result = std::make_shared<Cache>();
	result->sharedPatterns = std::move(shared);
	result->patternsByDialogId = std::move(byDialogId);
	result->exclusionsByDialogId = std::move(exclByDialogId);

	return result;
}

void rebuildCache() {
	std::lock_guard rebuildLock(rebuildMutex);
	auto next = buildCache();
	{
		std::lock_guard cacheLock(cacheMutex);
		std::lock_guard filteredLock(filteredMessagesMutex);
		cache = std::move(next);
		filteredMessages.clear();
		dialogsWithHiddenBlockedMessages.clear();
	}
}

std::unordered_map<long long, std::unordered_set<HashablePattern, PatternHasher>> buildExclusions(
	const std::vector<RegexFilterGlobalExclusion> &exclusions,
	const std::vector<HashablePattern> &shared) {
	std::unordered_map<long long, std::unordered_set<HashablePattern, PatternHasher>> exclusionsByDialogId;

	for (const auto &exclusion : exclusions) {
		auto &exclusionSet = exclusionsByDialogId[exclusion.dialogId];

		for (const auto &filter : shared) {
			if (filter.id == exclusion.filterId) {
				exclusionSet.insert(filter);
				break;
			}
		}
	}
	return exclusionsByDialogId;
}

std::shared_ptr<const Cache> snapshot() {
	{
		std::lock_guard lock(cacheMutex);
		if (cache) {
			return cache;
		}
	}

	std::lock_guard rebuildLock(rebuildMutex);
	{
		std::lock_guard lock(cacheMutex);
		if (cache) {
			return cache;
		}
	}

	auto next = buildCache();
	std::lock_guard lock(cacheMutex);
	if (!cache) {
		cache = std::move(next);
	}
	return cache;
}

std::optional<bool> isFiltered(not_null<HistoryItem*> item) {
	std::lock_guard lock(filteredMessagesMutex);
	auto dialogIt = filteredMessages.find(dialogKey(item->history()->peer));

	if (dialogIt == filteredMessages.end()) {
		return std::nullopt;
	}

	const auto it = dialogIt->second.find(item->id.bare);
	if (it == dialogIt->second.end()) {
		return std::nullopt;
	}

	return it->second;
}

bool hasFilteredMessages(not_null<PeerData*> peer) {
	std::lock_guard lock(filteredMessagesMutex);
	if (dialogsWithHiddenBlockedMessages.contains(dialogKey(peer))) {
		return true;
	}
	const auto dialogIt = filteredMessages.find(dialogKey(peer));
	if (dialogIt == filteredMessages.end()) {
		return false;
	}
	for (const auto &entry : dialogIt->second) {
		if (entry.second) {
			return true;
		}
	}
	return false;
}

void putHiddenBlockedMessage(not_null<HistoryItem*> item) {
	std::lock_guard lock(filteredMessagesMutex);
	dialogsWithHiddenBlockedMessages.insert(dialogKey(item->history()->peer));
}

void putFiltered(
		not_null<HistoryItem*> item,
		const Data::Group *group,
		bool res,
		const std::shared_ptr<const Cache> &matchedCache) {
	std::lock_guard cacheLock(cacheMutex);
	if (cache != matchedCache) {
		return;
	}

	std::lock_guard filteredLock(filteredMessagesMutex);
	filteredMessages[dialogKey(item->history()->peer)][item->id.bare] = res;
	if (group && res) {
		for (const auto& groupItem : group->items) {
			filteredMessages[dialogKey(item->history()->peer)][groupItem->id.bare] = true;
		}
	}
}

void invalidateSingle(not_null<HistoryItem*> item) {
	const auto dialogIt = filteredMessages.find(dialogKey(item->history()->peer));

	if (dialogIt == filteredMessages.end()) {
		return;
	}

	const auto it = dialogIt->second.find(item->id.bare);
	if (it == dialogIt->second.end()) {
		return;
	}

	dialogIt->second.erase(it);
}

void invalidate(not_null<HistoryItem*> item) {
	std::lock_guard lock(filteredMessagesMutex);
	if (const auto group = item->history()->owner().groups().find(item)) {
		for (const auto& groupItem : group->items) {
			invalidateSingle(groupItem);
		}
	} else {
		invalidateSingle(item);
	}
}

}
