// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026

#include "jel/features/filters/filters_controller.h"

#include "jel/jel_settings.h"
#include "jel/features/filters/filters_cache_controller.h"
#include "jel/features/filters/filters_utils.h"
#include "jel/utils/telegram_helpers.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "unicode/regex.h"

#include <algorithm>
#include <memory>
#include <set>
#include <unordered_set>

namespace FiltersController {

using DialogKey = std::pair<uint64, uint64>;
using MessageKey = std::pair<uint64, FullMsgId>;

std::set<DialogKey> showingFilteredMessages;
std::set<DialogKey> dialogsWithCollapsedDuplicates;
base::flat_set<MessageKey> notifiedDuplicates;
base::flat_set<MessageKey> removingDuplicates;
base::flat_set<MessageKey> expandedDuplicateGroups;

DialogKey dialogKey(not_null<PeerData*> peer) {
	return { peer->session().uniqueId(), peer->id.value };
}

MessageKey messageKey(not_null<const HistoryItem*> item) {
	return { item->history()->session().uniqueId(), item->fullId() };
}

void clearSession(uint64 sessionId) {
	std::erase_if(showingFilteredMessages, [=](const DialogKey &key) {
		return key.first == sessionId;
	});
	std::erase_if(dialogsWithCollapsedDuplicates, [=](const DialogKey &key) {
		return key.first == sessionId;
	});
	for (auto i = notifiedDuplicates.begin(); i != notifiedDuplicates.end();) {
		if (i->first == sessionId) {
			i = notifiedDuplicates.erase(i);
		} else {
			++i;
		}
	}
	for (auto i = removingDuplicates.begin(); i != removingDuplicates.end();) {
		if (i->first == sessionId) {
			i = removingDuplicates.erase(i);
		} else {
			++i;
		}
	}
	for (auto i = expandedDuplicateGroups.begin(); i != expandedDuplicateGroups.end();) {
		if (i->first == sessionId) {
			i = expandedDuplicateGroups.erase(i);
		} else {
			++i;
		}
	}
	FiltersCacheController::clearSession(sessionId);
}

void clearExpandedDuplicateGroups() {
	expandedDuplicateGroups.clear();
	notifiedDuplicates.clear();
	dialogsWithCollapsedDuplicates.clear();
}

bool filterBlocked(const not_null<HistoryItem*> item) {
	if (item->from() != item->history()->peer) {
		if (isBlocked(item)) {
			return true;
		}
	}
	if (const auto bot = item->viaBot()) {
		if (isBlocked(bot)) {
			return true;
		}
	}
	return false;
}

std::optional<bool> isFiltered(
		const QString &str,
		long long dialogId,
		const std::shared_ptr<const FiltersCacheController::Cache> &cache) {
	if (str.isEmpty()) {
		return std::nullopt;
	}

	const auto icuStr = icu::UnicodeString(reinterpret_cast<const UChar*>(str.constData()), str.length());

	const auto matches = [&](const ReversiblePattern &pattern)
	{
		UErrorCode status = U_ZERO_ERROR;

		const auto matcher = std::unique_ptr<icu::RegexMatcher>(pattern.pattern->matcher(icuStr, status));
		if (U_FAILURE(status) || !matcher) {
			LOG(("FILTER FAILED: %1").arg(u_errorName(status)));
			return false;
		}

		const auto match = matcher->find();
		const auto reversed = pattern.reversed;

		if ((!reversed && match) || (reversed && !match)) {
			return true;
		}
		return false;
	};

	if (const auto i = cache->patternsByDialogId.find(dialogId); i != cache->patternsByDialogId.end()) {
		for (const auto &pattern : i->second) {
			if (matches(pattern)) {
				return true;
			}
		}
	}

	const auto exclusions = cache->exclusionsByDialogId.find(dialogId);
	if (!cache->sharedPatterns.empty()) {
		for (const auto &pattern : cache->sharedPatterns) {
			if (exclusions != cache->exclusionsByDialogId.end() && exclusions->second.contains(pattern)) {
				continue;
			}
			if (matches(pattern.pattern)) {
				return true;
			}
		}
	}
	return false;
}

bool isEnabled(not_null<PeerData*> peer) {
	const auto &settings = JelSettings::getInstance();
	if (!settings.filtersEnabled()) {
		return false;
	}
	if (peer->isBroadcast()) {
		return settings.filtersEnabledInChannels();
	}
	if (peer->isChat() || peer->isMegagroup()) {
		return settings.filtersEnabledInGroups();
	}
	if (peer->isUser()) {
		return settings.filtersEnabledInPrivate();
	}
	return true;
}

bool isBlocked(const not_null<HistoryItem*> item) {
	const auto &settings = JelSettings::getInstance();

	auto shadowBanMatched = false;
	const auto blocked = [&]() -> bool
	{
		const auto isShadowBanned = [&](PeerData *peer) {
			return peer
				&& (peer->isUser() || peer->isBroadcast())
				&& settings.isShadowBanned(getDialogIdFromPeer(peer));
		};

		if (isShadowBanned(item->from())
			&& item->from()->id != item->history()->peer->id) {
			shadowBanMatched = true;
			return true;
		}

		if (item->from()->isUser()
			&& item->from()->asUser()->isBlocked()) {
			// don't hide messages if it's a dialog with blocked user
			return item->from()->asUser()->id != item->history()->peer->id;
		}

		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			if (const auto originalSender = forwarded->originalSender) {
				const auto originalShadowBanned = isShadowBanned(originalSender);
				if (originalShadowBanned
					|| (originalSender->isUser()
						&& originalSender->asUser()->isBlocked())) {
					shadowBanMatched = originalShadowBanned;
					return true;
				}
			}
		}
		return false;
	}();

	return settings.filtersEnabled()
		&& (shadowBanMatched || settings.hideFromBlocked())
		&& blocked;
}

bool isBlocked(const not_null<PeerData*> peer) {
	const auto &settings = JelSettings::getInstance();
	return settings.filtersEnabled() &&
	(
		(peer->isUser() && peer->asUser()->isBlocked() && settings.hideFromBlocked()) ||
		((peer->isUser() || peer->isBroadcast()) && settings.isShadowBanned(getDialogIdFromPeer(peer)))
	);
}

namespace {

constexpr auto kDuplicateFastLookupLimit = 200;
constexpr auto kMaxTrackedDuplicates = 2000;
constexpr auto kMaxExpandedDuplicateGroups = 1000;

enum class DuplicateLookupDirection {
	Previous,
	Next,
};

[[nodiscard]] bool DuplicateCollapsingEnabled(
		not_null<const HistoryItem*> item) {
	const auto peer = item->history()->peer;
	return JelSettings::getInstance().collapseDuplicates()
		&& isEnabled(peer)
		&& !showingFilteredMessages.contains(dialogKey(peer));
}

[[nodiscard]] bool IsRemoving(not_null<const HistoryItem*> item) {
	return removingDuplicates.contains(messageKey(item));
}

[[nodiscard]] bool HasNonEmptyReplyMarkup(not_null<const HistoryItem*> item) {
	const auto markup = item->Get<HistoryMessageReplyMarkup>();
	return markup && !markup->data.isNull();
}

[[nodiscard]] bool IsDuplicateContentEligible(
		not_null<const HistoryItem*> item) {
	return DuplicateCollapsingEnabled(item)
		&& item->isRegular()
		&& !item->isService()
		&& !item->isSponsored()
		&& !item->isLocal()
		&& !item->isSending()
		&& !item->hasFailed()
		&& !item->isPinned()
		&& !item->media()
		&& !HasNonEmptyReplyMarkup(item)
		&& (item->id > 0)
		&& !item->originalText().text.isEmpty();
}

[[nodiscard]] bool IsDuplicateCandidate(
		not_null<const HistoryItem*> item) {
	return !IsRemoving(item) && IsDuplicateContentEligible(item);
}

[[nodiscard]] bool SameThread(
		not_null<const HistoryItem*> first,
		not_null<const HistoryItem*> second) {
	return first->topicRootId() == second->topicRootId()
		&& first->sublistPeerId() == second->sublistPeerId();
}

[[nodiscard]] bool SameLocalDay(TimeId first, TimeId second) {
	if (first == second) {
		return true;
	}
	return base::unixtime::parse(first).date()
		== base::unixtime::parse(second).date();
}

[[nodiscard]] bool SameDuplicateContent(
		not_null<const HistoryItem*> first,
		not_null<const HistoryItem*> second) {
	if (!IsDuplicateContentEligible(first)
		|| !IsDuplicateContentEligible(second)) {
		return false;
	}
	if (!SameThread(first, second)
		|| !SameLocalDay(first->date(), second->date())) {
		return false;
	}
	if (first->isDeleted() != second->isDeleted()
		|| first->viaBot() != second->viaBot()) {
		return false;
	}
	const auto firstForward = first->Get<HistoryMessageForwarded>();
	const auto secondForward = second->Get<HistoryMessageForwarded>();
	if ((firstForward != nullptr) != (secondForward != nullptr)) {
		return false;
	}
	if (firstForward
		&& (!first->originalSender()
			|| first->originalSender() != second->originalSender()
			|| first->originalPostAuthor() != second->originalPostAuthor())) {
		return false;
	}
	const auto firstSigned = first->Get<HistoryMessageSigned>();
	const auto secondSigned = second->Get<HistoryMessageSigned>();
	if ((firstSigned != nullptr) != (secondSigned != nullptr)) {
		return false;
	}
	if (firstSigned
		&& (firstSigned->author != secondSigned->author
			|| firstSigned->viaBusinessBot != secondSigned->viaBusinessBot
			|| firstSigned->isAnonymousRank != secondSigned->isAnonymousRank)) {
		return false;
	}
	return first->from() == second->from()
		&& first->originalText() == second->originalText()
		&& first->replyTo() == second->replyTo();
}

[[nodiscard]] bool IsAdjacentCandidate(
		not_null<const HistoryItem*> origin,
		not_null<const HistoryItem*> candidate) {
	return candidate->id > 0
		&& candidate->isRegular()
		&& !IsRemoving(candidate)
		&& !candidate->isService()
		&& SameThread(origin, candidate);
}

[[nodiscard]] HistoryItem *LookupClosestLoadedMessage(
		not_null<const HistoryItem*> item,
		DuplicateLookupDirection direction) {
	auto result = static_cast<HistoryItem*>(nullptr);
	for (const auto &entry : item->history()->items()) {
		const auto candidate = entry.get();
		if (candidate == item.get() || !IsAdjacentCandidate(item, candidate)) {
			continue;
		}
		if (direction == DuplicateLookupDirection::Previous) {
			if (candidate->id < item->id
				&& (!result || candidate->id > result->id)) {
				result = candidate;
			}
		} else if (candidate->id > item->id
			&& (!result || candidate->id < result->id)) {
			result = candidate;
		}
	}
	return result;
}

[[nodiscard]] HistoryItem *LookupAdjacentMessage(
		not_null<const HistoryItem*> item,
		DuplicateLookupDirection direction) {
	if (item->id <= 0) {
		return nullptr;
	}
	if (const auto view = item->mainView()) {
		auto current = (direction == DuplicateLookupDirection::Previous)
			? view->previousInBlocks()
			: view->nextInBlocks();
		while (current) {
			const auto candidate = current->data().get();
			if (IsAdjacentCandidate(item, candidate)) {
				return candidate;
			}
			current = (direction == DuplicateLookupDirection::Previous)
				? current->previousInBlocks()
				: current->nextInBlocks();
		}
	}
	const auto peerId = item->history()->peer->id;
	const auto &owner = item->history()->owner();
	for (auto offset = 1; offset <= kDuplicateFastLookupLimit; ++offset) {
		const auto id = (direction == DuplicateLookupDirection::Previous)
			? (item->id - offset)
			: (item->id + offset);
		if (id <= 0) {
			break;
		}
		const auto adjacent = owner.message(peerId, id);
		if (!adjacent || !IsAdjacentCandidate(item, adjacent)) {
			continue;
		}
		return adjacent;
	}
	return LookupClosestLoadedMessage(item, direction);
}

void NotifyDuplicateHead(
		not_null<const HistoryItem*> duplicate,
		not_null<const HistoryItem*> head) {
	const auto duplicateKey = messageKey(duplicate);
	if (notifiedDuplicates.contains(duplicateKey)) {
		return;
	}
	if (notifiedDuplicates.size() >= kMaxTrackedDuplicates) {
		notifiedDuplicates.clear();
	}
	notifiedDuplicates.emplace(duplicateKey);

	const auto session = &head->history()->session();
	const auto owner = &head->history()->owner();
	const auto headId = head->fullId();
	crl::on_main(session, [=] {
		if (const auto current = owner->message(headId)) {
			owner->requestItemViewRefresh(current);
		}
	});
}

void RefreshDuplicateGroupSync(
		const std::vector<not_null<HistoryItem*>> &group) {
	if (group.empty()) {
		return;
	}
	const auto owner = &group.front()->history()->owner();
	auto ids = std::vector<FullMsgId>();
	ids.reserve(group.size() + 2);
	const auto appendUnique = [&](const HistoryItem *entry) {
		if (!entry) {
			return;
		}
		const auto id = entry->fullId();
		if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
			ids.push_back(id);
		}
	};
	for (const auto &entry : group) {
		appendUnique(entry);
	}
	appendUnique(LookupAdjacentMessage(
		group.back(),
		DuplicateLookupDirection::Next));
	for (const auto &id : ids) {
		if (const auto current = owner->message(id)) {
			owner->requestItemViewRefresh(current);
		}
	}
}

} // namespace

const HistoryItem *getDuplicateHead(
		const not_null<const HistoryItem*> item) {
	if (!IsDuplicateCandidate(item)) {
		return nullptr;
	}
	const auto previous = LookupAdjacentMessage(
		item,
		DuplicateLookupDirection::Previous);
	if (!previous || !SameDuplicateContent(previous, item)) {
		return nullptr;
	}

	auto head = static_cast<const HistoryItem*>(previous);
	while (const auto earlier = LookupAdjacentMessage(
			head,
			DuplicateLookupDirection::Previous)) {
		if (!SameDuplicateContent(earlier, item)) {
			break;
		}
		head = earlier;
	}
	return head;
}

bool isDuplicateGroupExpanded(const not_null<const HistoryItem*> item) {
	if (!IsDuplicateCandidate(item)) {
		return false;
	}
	const auto head = getDuplicateHead(item);
	const auto first = head ? head : item.get();
	return expandedDuplicateGroups.contains(messageKey(first));
}

bool isCollapsedDuplicate(const not_null<const HistoryItem*> item) {
	const auto head = getDuplicateHead(item);
	return head && !expandedDuplicateGroups.contains(messageKey(head));
}

bool isDuplicateMessage(const not_null<HistoryItem*> item) {
	const auto head = getDuplicateHead(item);
	if (!head || expandedDuplicateGroups.contains(messageKey(head))) {
		return false;
	}
	dialogsWithCollapsedDuplicates.insert(dialogKey(item->history()->peer));
	NotifyDuplicateHead(item, head);
	return true;
}

std::vector<not_null<HistoryItem*>> getDuplicateGroup(
		not_null<HistoryItem*> item) {
	auto result = std::vector<not_null<HistoryItem*>>();
	if (!IsDuplicateCandidate(item)) {
		result.push_back(item);
		return result;
	}

	const auto head = getDuplicateHead(item);
	const auto first = head ? const_cast<HistoryItem*>(head) : item.get();
	result.push_back(first);

	for (auto next = LookupAdjacentMessage(
			first,
			DuplicateLookupDirection::Next);
		next && SameDuplicateContent(first, next);
		next = LookupAdjacentMessage(
			next,
			DuplicateLookupDirection::Next)) {
		result.push_back(next);
	}
	return result;
}

int countDuplicateGroupSize(const not_null<HistoryItem*> item) {
	return static_cast<int>(getDuplicateGroup(item).size());
}

int collapsedDuplicateGroupSize(const not_null<const HistoryItem*> item) {
	if (!IsDuplicateCandidate(item)) {
		return 1;
	}
	if (getDuplicateHead(item) != nullptr) {
		return 1;
	}
	if (expandedDuplicateGroups.contains(messageKey(item))) {
		return 1;
	}
	auto count = 1;
	for (auto next = LookupAdjacentMessage(
			item,
			DuplicateLookupDirection::Next);
		next && SameDuplicateContent(item, next);
		next = LookupAdjacentMessage(
			next,
			DuplicateLookupDirection::Next)) {
		++count;
	}
	return count;
}

void expandDuplicateGroup(not_null<HistoryItem*> item) {
	const auto group = getDuplicateGroup(item);
	if (group.size() <= 1) {
		return;
	}
	const auto headKey = messageKey(group.front());
	if (expandedDuplicateGroups.contains(headKey)) {
		return;
	}
	if (expandedDuplicateGroups.size() >= kMaxExpandedDuplicateGroups) {
		expandedDuplicateGroups.clear();
	}
	expandedDuplicateGroups.emplace(headKey);
	RefreshDuplicateGroupSync(group);
}

void collapseDuplicateGroup(not_null<HistoryItem*> item) {
	const auto group = getDuplicateGroup(item);
	if (group.size() <= 1) {
		return;
	}
	const auto headKey = messageKey(group.front());
	if (!expandedDuplicateGroups.contains(headKey)) {
		return;
	}
	expandedDuplicateGroups.remove(headKey);
	for (const auto &entry : group) {
		notifiedDuplicates.remove(messageKey(entry));
	}
	RefreshDuplicateGroupSync(group);
}

void toggleDuplicateGroup(not_null<HistoryItem*> item) {
	if (isDuplicateGroupExpanded(item)) {
		collapseDuplicateGroup(item);
	} else {
		expandDuplicateGroup(item);
	}
}

void handleDuplicateItemRemoved(not_null<const HistoryItem*> item) {
	const auto itemKey = messageKey(item);
	notifiedDuplicates.remove(itemKey);
	const auto wasExpandedHead = expandedDuplicateGroups.contains(itemKey);
	expandedDuplicateGroups.remove(itemKey);
	if (!item->isRegular() || !DuplicateCollapsingEnabled(item)) {
		return;
	}

	removingDuplicates.emplace(itemKey);
	const auto previous = LookupAdjacentMessage(
		item,
		DuplicateLookupDirection::Previous);
	const auto next = LookupAdjacentMessage(
		item,
		DuplicateLookupDirection::Next);

	auto refreshIds = std::vector<FullMsgId>();
	const auto appendRefresh = [&](const HistoryItem *entry) {
		if (!entry) {
			return;
		}
		const auto id = entry->fullId();
		if (std::find(refreshIds.begin(), refreshIds.end(), id) == refreshIds.end()) {
			refreshIds.push_back(id);
		}
	};

	if (previous && SameDuplicateContent(previous, item)) {
		const auto head = getDuplicateHead(previous);
		appendRefresh(head ? head : previous);
		if (next && SameDuplicateContent(next, item)) {
			appendRefresh(next);
		}
	} else if (next && SameDuplicateContent(next, item)) {
		if (wasExpandedHead) {
			expandedDuplicateGroups.emplace(messageKey(next));
		}
		notifiedDuplicates.remove(messageKey(next));
		appendRefresh(next);
		for (auto cur = LookupAdjacentMessage(
				next,
				DuplicateLookupDirection::Next);
			cur && SameDuplicateContent(next, cur);
			cur = LookupAdjacentMessage(cur, DuplicateLookupDirection::Next)) {
			notifiedDuplicates.remove(messageKey(cur));
			appendRefresh(cur);
		}
	} else if (previous && next && SameDuplicateContent(previous, next)) {
		const auto head = getDuplicateHead(previous);
		appendRefresh(head ? head : previous);
		notifiedDuplicates.remove(messageKey(next));
		appendRefresh(next);
		for (auto cur = LookupAdjacentMessage(
				next,
				DuplicateLookupDirection::Next);
			cur && SameDuplicateContent(next, cur);
			cur = LookupAdjacentMessage(cur, DuplicateLookupDirection::Next)) {
			notifiedDuplicates.remove(messageKey(cur));
			appendRefresh(cur);
		}
	}

	const auto session = &item->history()->session();
	const auto owner = &item->history()->owner();
	const auto weakSession = base::make_weak(session);
	crl::on_main([=] {
		removingDuplicates.remove(itemKey);
		if (!weakSession) {
			return;
		}
		for (const auto &id : refreshIds) {
			if (const auto current = owner->message(id)) {
				owner->requestItemViewRefresh(current);
			}
		}
	});
}

bool isBlockedOrRegexFiltered(const not_null<HistoryItem*> item) {
	const auto &settings = JelSettings::getInstance();
	if (!settings.filtersEnabled()) {
		return false;
	}

	if (item->out()) {
		return false;
	}

	if (filterBlocked(item)) {
		FiltersCacheController::putHiddenBlockedMessage(item);
		return true;
	}

	if (!isEnabled(item->history()->peer)) return false;

	const auto cached = FiltersCacheController::isFiltered(item);
	if (cached.has_value()) {
		return cached.value();
	}
	const auto group = item->history()->owner().groups().find(item);
	const auto cache = FiltersCacheController::snapshot();
	const auto res = isFiltered(
		FilterUtils::extractAllText(item, group),
		getDialogIdFromPeer(item->history()->peer),
		cache);

	if (res.has_value()) {
		FiltersCacheController::putFiltered(item, group, res.value(), cache);
		return res.value();
	}
	return false;
}

bool filtered(const not_null<HistoryItem*> item) {
	if (showingFilteredMessages.contains(dialogKey(item->history()->peer))) {
		return false;
	}

	const auto &settings = JelSettings::getInstance();
	if (!settings.filtersEnabled()) {
		return false;
	}

	if (!isEnabled(item->history()->peer)) return false;

	if (isBlockedOrRegexFiltered(item)) {
		return true;
	}

	return isDuplicateMessage(item);
}

std::optional<bool> filteredMessagesShown(not_null<PeerData*> peer) {
	const auto key = dialogKey(peer);
	if (!showingFilteredMessages.contains(key)
		&& !FiltersCacheController::hasFilteredMessages(peer)
		&& !dialogsWithCollapsedDuplicates.contains(key)) {
		return std::nullopt;
	}
	return showingFilteredMessages.contains(key);
}

void toggleFilteredMessagesShown(not_null<PeerData*> peer) {
	const auto key = dialogKey(peer);
	if (showingFilteredMessages.contains(key)) {
		showingFilteredMessages.erase(key);
	} else {
		showingFilteredMessages.insert(key);
	}
	FiltersCacheController::fireUpdate();
}

void invalidate(not_null<HistoryItem*> item) {
	const auto &settings = JelSettings::getInstance();
	if (!settings.filtersEnabled()) {
		return;
	}

	FiltersCacheController::invalidate(item);

	if (!item->isRegular() || !DuplicateCollapsingEnabled(item)) {
		return;
	}

	auto refreshIds = std::vector<FullMsgId>();
	const auto appendRefresh = [&](const HistoryItem *entry) {
		if (!entry) {
			return;
		}
		const auto id = entry->fullId();
		if (std::find(refreshIds.begin(), refreshIds.end(), id) == refreshIds.end()) {
			refreshIds.push_back(id);
		}
	};

	const auto oldGroup = getDuplicateGroup(item);
	for (const auto &entry : oldGroup) {
		notifiedDuplicates.remove(messageKey(entry));
		appendRefresh(entry);
	}
	appendRefresh(LookupAdjacentMessage(item, DuplicateLookupDirection::Previous));
	appendRefresh(LookupAdjacentMessage(item, DuplicateLookupDirection::Next));
	appendRefresh(item);

	const auto session = &item->history()->session();
	const auto owner = &item->history()->owner();
	const auto itemId = item->fullId();
	crl::on_main(session, [=] {
		auto toRefresh = refreshIds;
		const auto addUnique = [&](const HistoryItem *entry) {
			if (!entry) {
				return;
			}
			const auto id = entry->fullId();
			if (std::find(toRefresh.begin(), toRefresh.end(), id) == toRefresh.end()) {
				toRefresh.push_back(id);
			}
		};
		if (const auto current = owner->message(itemId)) {
			const auto newGroup = getDuplicateGroup(current);
			for (const auto &entry : newGroup) {
				notifiedDuplicates.remove(messageKey(entry));
				addUnique(entry);
			}
			if (!newGroup.empty()) {
				addUnique(LookupAdjacentMessage(
					newGroup.back(),
					DuplicateLookupDirection::Next));
			}
		}
		for (const auto &id : toRefresh) {
			if (const auto current = owner->message(id)) {
				owner->requestItemViewRefresh(current);
			}
		}
	});
}

}
