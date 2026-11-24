#include <algorithm>
#include <cassert>
#include <compare>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using uint64 = std::uint64_t;
using TimeId = std::int32_t;

template <typename T> class not_null {
	T ptr;
public:
	not_null(T p) : ptr(p) { assert(p); }
	template <typename U> not_null(const not_null<U> &other) : ptr(other.get()) {}
	T get() const { return ptr; }
	T operator->() const { return ptr; }
	operator T() const { return ptr; }
};

struct PeerId {
	uint64 value = 1;
	bool operator==(const PeerId &) const = default;
};

struct Session {
	uint64 id = 1;
	uint64 uniqueId() const { return id; }
};

struct PeerData {
	PeerId id;
	Session *s = nullptr;
	Session &session() const {
		static Session fallback;
		return s ? *s : fallback;
	}
};

struct FullMsgId {
	uint64 peer = 0;
	int msg = 0;
	auto operator<=>(const FullMsgId &) const = default;
};

struct HistoryMessageForwarded {};
struct HistoryMessageMarkupData {
	bool empty = true;
	bool isNull() const { return empty; }
};
struct HistoryMessageReplyMarkup {
	HistoryMessageMarkupData data;
};
struct HistoryMessageSigned {
	std::string author;
	PeerData *viaBusinessBot = nullptr;
	bool isAnonymousRank = false;
};

struct String : std::string {
	using std::string::string;
	using std::string::operator=;
	bool isEmpty() const { return empty(); }
};
struct Text {
	String text;
	bool operator==(const Text &) const = default;
};

struct HistoryItem;
struct Owner;
struct ViewElement {
	HistoryItem *item = nullptr;
	ViewElement *prev = nullptr;
	ViewElement *next = nullptr;
	not_null<HistoryItem*> data() const { return item; }
	ViewElement *previousInBlocks() const { return prev; }
	ViewElement *nextInBlocks() const { return next; }
};

struct History {
	PeerData *peer;
	Owner *data;
	std::vector<std::unique_ptr<HistoryItem>> entries;
	const auto &items() const { return entries; }
	Owner &owner() const { return *data; }
	Session &session() const { return peer->session(); }
};

struct HistoryItem {
	int id = 0;
	History *h = nullptr;
	bool regular = true;
	bool service = false;
	bool sponsored = false;
	bool local = false;
	bool sending = false;
	bool failed = false;
	bool pinned = false;
	bool deleted = false;
	bool hasMedia = false;
	PeerData *sender = nullptr;
	PeerData *original = nullptr;
	PeerData *bot = nullptr;
	bool forwarded = false;
	std::string signature;
	HistoryMessageReplyMarkup markup{};
	bool hasMarkup = false;
	HistoryMessageSigned signedInfo{};
	bool hasSigned = false;
	Text text{ "same message" };
	int reply = 0;
	int topicRoot = 0;
	PeerId sublist{ 0 };
	TimeId timestamp = 1700000000;
	ViewElement *view = nullptr;

	History *history() const { return h; }
	bool isRegular() const { return regular; }
	bool isService() const { return service; }
	bool isSponsored() const { return sponsored; }
	bool isLocal() const { return local; }
	bool isSending() const { return sending; }
	bool hasFailed() const { return failed; }
	bool isPinned() const { return pinned; }
	bool isDeleted() const { return deleted; }
	const void *media() const { return hasMedia ? this : nullptr; }
	const Text &originalText() const { return text; }
	PeerData *from() const { return sender; }
	PeerData *originalSender() const { return original; }
	PeerData *viaBot() const { return bot; }
	const std::string &originalPostAuthor() const { return signature; }
	int replyTo() const { return reply; }
	int topicRootId() const { return topicRoot; }
	PeerId sublistPeerId() const { return sublist; }
	TimeId date() const { return timestamp; }
	ViewElement *mainView() const { return view; }
	FullMsgId fullId() const { return { h->peer->id.value, id }; }

	template <typename T> const T *Get() const {
		if constexpr (std::is_same_v<T, HistoryMessageForwarded>) {
			static T value;
			return forwarded ? &value : nullptr;
		} else if constexpr (std::is_same_v<T, HistoryMessageReplyMarkup>) {
			return hasMarkup ? &markup : nullptr;
		} else if constexpr (std::is_same_v<T, HistoryMessageSigned>) {
			return hasSigned ? &signedInfo : nullptr;
		} else {
			return nullptr;
		}
	}
};

struct Owner {
	History *h = nullptr;
	int refreshCount = 0;
	std::vector<int> refreshedIds;
	HistoryItem *message(PeerId, int id) const {
		for (const auto &item : h->entries) {
			if (item->id == id) {
				return item.get();
			}
		}
		return nullptr;
	}
	HistoryItem *message(FullMsgId id) const { return message({}, id.msg); }
	void requestItemViewRefresh(HistoryItem *item) {
		++refreshCount;
		refreshedIds.push_back(item->id);
	}
};

namespace base {
template <typename T> struct flat_set : std::set<T> {
	void remove(const T &v) { this->erase(v); }
};
template <typename T> T *make_weak(T *ptr) { return ptr; }
namespace unixtime {
struct ParsedDate {
	int day = 0;
	int date() const { return day; }
};
inline ParsedDate parse(TimeId ts) {
	return { ts / 86400 };
}
} // namespace unixtime
} // namespace base

namespace crl {
inline std::vector<std::function<void()>> &queue() {
	static std::vector<std::function<void()>> q;
	return q;
}
inline void flush() {
	while (!queue().empty()) {
		auto pending = std::move(queue());
		queue().clear();
		for (auto &fn : pending) {
			fn();
		}
	}
}
template <typename Guard, typename F> void on_main(Guard *, F fn) {
	queue().push_back(std::move(fn));
}
template <typename F> void on_main(F fn) {
	queue().push_back(std::move(fn));
}
} // namespace crl

struct JelSettings {
	static JelSettings &getInstance() { static JelSettings s; return s; }
	bool collapseDuplicates() const { return true; }
};

namespace FiltersController {
using DialogKey = std::pair<uint64, uint64>;
using MessageKey = std::pair<uint64, FullMsgId>;

std::set<DialogKey> showingFilteredMessages;
std::set<DialogKey> dialogsWithCollapsedDuplicates;
base::flat_set<MessageKey> notifiedDuplicates;
base::flat_set<MessageKey> removingDuplicates;
base::flat_set<MessageKey> expandedDuplicateGroups;

DialogKey dialogKey(PeerData *peer) {
	return { peer->session().uniqueId(), peer->id.value };
}
MessageKey messageKey(not_null<const HistoryItem*> item) {
	return { item->history()->session().uniqueId(), item->fullId() };
}
bool isEnabled(PeerData *) { return true; }
#include "duplicate_group_under_test.h"
} // namespace FiltersController

int main() {
	using namespace FiltersController;
	Session sessionA{ 1 };
	Session sessionB{ 2 };
	PeerData chat{ { 100 }, &sessionA };
	PeerData self{ { 1 }, &sessionA };
	PeerData alice{ { 2 }, &sessionA };
	PeerData bob{ { 3 }, &sessionA };
	PeerData botA{ { 4 }, &sessionA };
	PeerData botB{ { 5 }, &sessionA };
	Owner owner{};
	History history{ &chat, &owner, {} };
	owner.h = &history;

	auto add = [&](int id, bool real, PeerData *author, bool forward = true) {
		auto item = std::make_unique<HistoryItem>();
		item->id = id;
		item->h = &history;
		item->regular = real;
		item->sender = &self;
		item->original = author;
		item->forwarded = forward;
		auto result = item.get();
		history.entries.push_back(std::move(item));
		return result;
	};

	auto first = add(1, true, &alice);
	auto second = add(2, true, &bob);
	assert(countDuplicateGroupSize(first) == 1);
	assert(countDuplicateGroupSize(second) == 1);
	assert(!getDuplicateHead(second));

	second->original = &alice;
	assert(countDuplicateGroupSize(first) == 2);
	assert(collapsedDuplicateGroupSize(first) == 2);
	assert(collapsedDuplicateGroupSize(second) == 1);
	assert(getDuplicateHead(second) == first);

	// Non-regular preview messages do not join or split groups.
	auto previewA = add(1000, false, &alice);
	auto previewB = add(1001, false, &alice);
	assert(countDuplicateGroupSize(first) == 2);
	assert(countDuplicateGroupSize(previewA) == 1);
	assert(countDuplicateGroupSize(previewB) == 1);
	assert(!isDuplicateMessage(previewA));

	auto realThird = add(2000, true, &alice);
	assert(countDuplicateGroupSize(first) == 3);
	assert(collapsedDuplicateGroupSize(first) == 3);
	assert(getDuplicateHead(realThird) == first);
	assert(countDuplicateGroupSize(previewA) == 1);

	// Forwarded without originalSender does not collapse.
	first->original = nullptr;
	second->original = nullptr;
	realThird->original = nullptr;
	assert(countDuplicateGroupSize(first) == 1);
	assert(!getDuplicateHead(second));

	// Plain text messages collapse.
	first->forwarded = false;
	second->forwarded = false;
	realThird->forwarded = false;
	assert(countDuplicateGroupSize(first) == 3);

	// Expand / collapse toggle behavior.
	assert(!isDuplicateGroupExpanded(first));
	assert(isCollapsedDuplicate(second));
	assert(isCollapsedDuplicate(realThird));
	assert(!isCollapsedDuplicate(first));
	assert(isDuplicateMessage(second));
	assert(dialogsWithCollapsedDuplicates.contains(dialogKey(&chat)));

	expandDuplicateGroup(first);
	assert(isDuplicateGroupExpanded(first));
	assert(isDuplicateGroupExpanded(second));
	assert(!isCollapsedDuplicate(second));
	assert(!isDuplicateMessage(second));
	assert(collapsedDuplicateGroupSize(first) == 1);
	assert(countDuplicateGroupSize(first) == 3);

	collapseDuplicateGroup(realThird);
	assert(!isDuplicateGroupExpanded(first));
	assert(isCollapsedDuplicate(second));
	assert(collapsedDuplicateGroupSize(first) == 3);

	toggleDuplicateGroup(second);
	assert(isDuplicateGroupExpanded(first));
	toggleDuplicateGroup(first);
	assert(!isDuplicateGroupExpanded(first));

	// Different calendar days do not collapse.
	realThird->timestamp = first->timestamp + 86400;
	assert(countDuplicateGroupSize(first) == 2);
	assert(!getDuplicateHead(realThird));
	realThird->timestamp = first->timestamp;
	assert(countDuplicateGroupSize(first) == 3);

	// Different forum topics are isolated and do not break other topics' chains.
	second->topicRoot = 99;
	assert(countDuplicateGroupSize(first) == 2);
	assert(getDuplicateHead(realThird) == first);
	assert(countDuplicateGroupSize(second) == 1);
	assert(!getDuplicateHead(second));
	second->topicRoot = 0;
	assert(countDuplicateGroupSize(first) == 3);

	// Different monoforum sublists do not collapse.
	second->sublist = { 55 };
	assert(countDuplicateGroupSize(first) == 2);
	assert(countDuplicateGroupSize(second) == 1);
	second->sublist = { 0 };
	assert(countDuplicateGroupSize(first) == 3);

	// Pinned messages never collapse.
	second->pinned = true;
	assert(countDuplicateGroupSize(first) == 1);
	assert(countDuplicateGroupSize(second) == 1);
	assert(!isDuplicateMessage(second));
	second->pinned = false;
	assert(countDuplicateGroupSize(first) == 3);

	// Messages with inline reply markup never collapse.
	second->hasMarkup = true;
	second->markup.data.empty = false;
	assert(countDuplicateGroupSize(first) == 1);
	assert(countDuplicateGroupSize(second) == 1);
	second->hasMarkup = false;
	assert(countDuplicateGroupSize(first) == 3);

	// Different signed authors / ranks do not collapse.
	first->hasSigned = true;
	first->signedInfo.author = "Admin A";
	second->hasSigned = true;
	second->signedInfo.author = "Admin B";
	assert(countDuplicateGroupSize(first) == 1);
	second->signedInfo.author = "Admin A";
	realThird->hasSigned = true;
	realThird->signedInfo.author = "Admin A";
	assert(countDuplicateGroupSize(first) == 3);
	first->hasSigned = second->hasSigned = realThird->hasSigned = false;

	// Live vs saved-deleted messages do not collapse.
	second->deleted = true;
	assert(countDuplicateGroupSize(first) == 1);
	second->deleted = false;
	assert(countDuplicateGroupSize(first) == 3);

	// Different viaBot do not collapse.
	second->bot = &botA;
	assert(countDuplicateGroupSize(first) == 1);
	first->bot = &botA;
	second->bot = &botB;
	assert(countDuplicateGroupSize(first) == 1);
	first->bot = second->bot = nullptr;
	assert(countDuplicateGroupSize(first) == 3);

	// Mixed forwarded/plain, post author, reply, text, media checks.
	second->forwarded = true;
	second->original = &alice;
	assert(countDuplicateGroupSize(first) == 1);
	first->forwarded = true;
	first->original = &alice;
	first->signature = "author A";
	second->signature = "author B";
	assert(countDuplicateGroupSize(first) == 1);
	first->signature = second->signature;
	assert(countDuplicateGroupSize(first) == 2);
	second->reply = 7;
	assert(countDuplicateGroupSize(first) == 1);
	second->reply = 0;
	second->text.text = "different";
	assert(countDuplicateGroupSize(first) == 1);
	second->text = first->text;
	first->hasMedia = true;
	assert(countDuplicateGroupSize(first) == 1);
	assert(!getDuplicateHead(second));
	first->hasMedia = false;
	second->hasMedia = true;
	assert(countDuplicateGroupSize(first) == 1);
	assert(!isDuplicateMessage(second));
	first->hasMedia = true;
	assert(countDuplicateGroupSize(first) == 1);
	assert(countDuplicateGroupSize(second) == 1);
	first->hasMedia = second->hasMedia = false;

	// Multi-account isolation: expanding in sessionA does not affect sessionB.
	PeerData chatB{ { 100 }, &sessionB };
	PeerData selfB{ { 1 }, &sessionB };
	Owner ownerB{};
	History historyB{ &chatB, &ownerB, {} };
	ownerB.h = &historyB;
	auto b1 = std::make_unique<HistoryItem>();
	b1->id = 1; b1->h = &historyB; b1->sender = &selfB;
	auto b2 = std::make_unique<HistoryItem>();
	b2->id = 2; b2->h = &historyB; b2->sender = &selfB;
	auto b1p = b1.get(); auto b2p = b2.get();
	historyB.entries.push_back(std::move(b1));
	historyB.entries.push_back(std::move(b2));
	expandDuplicateGroup(first);
	assert(isDuplicateGroupExpanded(first));
	assert(!isDuplicateGroupExpanded(b1p));
	assert(isCollapsedDuplicate(b2p));

	auto eraseItem = [&](HistoryItem *target) {
		history.entries.erase(
			std::remove_if(
				history.entries.begin(),
				history.entries.end(),
				[&](const auto &entry) { return entry.get() == target; }),
			history.entries.end());
	};

	// Removing an expanded head transfers expanded state to the new head.
	collapseDuplicateGroup(first);
	first->forwarded = second->forwarded = realThird->forwarded = false;
	expandDuplicateGroup(first);
	assert(isDuplicateGroupExpanded(first));
	owner.refreshedIds.clear();
	const auto firstKey = messageKey(first);
	handleDuplicateItemRemoved(first);
	eraseItem(first);
	crl::flush();
	assert(isDuplicateGroupExpanded(second));
	assert(!removingDuplicates.contains(firstKey));
	assert(std::find(owner.refreshedIds.begin(), owner.refreshedIds.end(), second->id) != owner.refreshedIds.end());

	// Removing an unrelated message between two identical messages refreshes both sides.
	collapseDuplicateGroup(second);
	auto mid = add(500, true, nullptr, false);
	mid->text.text = "separator";
	owner.refreshedIds.clear();
	const auto midKey = messageKey(mid);
	handleDuplicateItemRemoved(mid);
	eraseItem(mid);
	crl::flush();
	assert(!removingDuplicates.contains(midKey));
	assert(std::find(owner.refreshedIds.begin(), owner.refreshedIds.end(), second->id) != owner.refreshedIds.end());
	assert(std::find(owner.refreshedIds.begin(), owner.refreshedIds.end(), realThird->id) != owner.refreshedIds.end());
	assert(collapsedDuplicateGroupSize(second) == 2);
	assert(isCollapsedDuplicate(realThird));

	std::cout << "All duplicate grouping, isolation, edge-case, removal, and expand/collapse checks passed.\n";
}
