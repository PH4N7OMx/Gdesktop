#include "search_filters_core.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

using namespace Jel;

static void TestDatePacking() {
	assert(PackSearchDate(2026, 4, 16) == 20260416);
	assert(IsValidPackedSearchDate(20260416));
	assert(!IsValidPackedSearchDate(0));
	assert(!IsValidPackedSearchDate(-1));
	assert(!IsValidPackedSearchDate(20261301));
	assert(!IsValidPackedSearchDate(20260010));
	assert(!IsValidPackedSearchDate(20260432));
	assert(PackSearchDate(1969, 12, 31) == 0);
	assert(PackSearchDate(2026, 13, 1) == 0);
}

static void TestDefaultAndActiveCount() {
	SearchExtraFilters f;
	assert(f.isDefault());
	assert(f.activeCount() == 0);
	assert(!f.hasDateFilter());
	assert(!f.triggersEmptyQuerySearch());
	assert(!f.isOldestFirst());
	assert(!f.hasClientSideSenderFilter());

	f.sortOrder = SearchSortOrder::OldestFirst;
	assert(!f.isDefault());
	assert(f.activeCount() == 1);
	assert(f.isOldestFirst());
	assert(!f.triggersEmptyQuerySearch());

	f.mediaFilter = SearchMediaFilter::Links;
	assert(f.activeCount() == 2);
	assert(f.triggersEmptyQuerySearch());

	f.datePreset = SearchDatePreset::Last7Days;
	assert(f.hasDateFilter());
	assert(f.activeCount() == 3);

	f.senderFilter = SearchSenderFilter::MyMessages;
	assert(f.hasClientSideSenderFilter());
	assert(f.activeCount() == 4);

	f.reset();
	assert(f.isDefault());
	assert(f.activeCount() == 0);
}

static void TestNormalizeDates() {
	SearchExtraFilters f;
	f.datePreset = SearchDatePreset::CustomRange;
	f.customFromDate = 20260420;
	f.customToDate = 20260410;
	f.normalizeDates();
	assert(f.datePreset == SearchDatePreset::CustomRange);
	assert(f.customFromDate == 20260410);
	assert(f.customToDate == 20260420);

	// Single bound fills the other.
	f.customFromDate = 20260415;
	f.customToDate = 0;
	f.normalizeDates();
	assert(f.customFromDate == 20260415);
	assert(f.customToDate == 20260415);

	// Both invalid resets to AnyTime.
	f.customFromDate = 0;
	f.customToDate = 0;
	f.normalizeDates();
	assert(f.datePreset == SearchDatePreset::AnyTime);
	assert(!f.hasDateFilter());

	// Non-custom preset clears custom fields.
	f.datePreset = SearchDatePreset::Today;
	f.customFromDate = 20260415;
	f.customToDate = 20260416;
	f.normalizeDates();
	assert(f.customFromDate == 0);
	assert(f.customToDate == 0);
}

static void TestSanitizeForContext() {
	// Private DM preserves everything.
	SearchExtraFilters f;
	f.sortOrder = SearchSortOrder::OldestFirst;
	f.mediaFilter = SearchMediaFilter::Pinned;
	f.datePreset = SearchDatePreset::Last30Days;
	f.senderFilter = SearchSenderFilter::PeerMessages;

	auto dm = f;
	dm.sanitizeForContext(/*inPeer=*/true, /*isPrivateUser=*/true);
	assert(dm.sortOrder == SearchSortOrder::OldestFirst);
	assert(dm.mediaFilter == SearchMediaFilter::Pinned);
	assert(dm.datePreset == SearchDatePreset::Last30Days);
	assert(dm.senderFilter == SearchSenderFilter::PeerMessages);

	// Group / Channel clears DM senderFilter, keeps OldestFirst + Pinned + Date.
	auto grp = f;
	grp.sanitizeForContext(/*inPeer=*/true, /*isPrivateUser=*/false);
	assert(grp.sortOrder == SearchSortOrder::OldestFirst);
	assert(grp.mediaFilter == SearchMediaFilter::Pinned);
	assert(grp.datePreset == SearchDatePreset::Last30Days);
	assert(grp.senderFilter == SearchSenderFilter::All);

	// Global search resets OldestFirst, senderFilter, Pinned/Polls, keeps Date + Links.
	auto globalPinned = f;
	globalPinned.sanitizeForContext(/*inPeer=*/false, /*isPrivateUser=*/false);
	assert(globalPinned.sortOrder == SearchSortOrder::NewestFirst);
	assert(globalPinned.mediaFilter == SearchMediaFilter::All);
	assert(globalPinned.datePreset == SearchDatePreset::Last30Days);
	assert(globalPinned.senderFilter == SearchSenderFilter::All);

	auto globalPolls = f;
	globalPolls.mediaFilter = SearchMediaFilter::Polls;
	globalPolls.sanitizeForContext(/*inPeer=*/false, /*isPrivateUser=*/false);
	assert(globalPolls.mediaFilter == SearchMediaFilter::All);

	auto globalLinks = f;
	globalLinks.mediaFilter = SearchMediaFilter::Links;
	globalLinks.sanitizeForContext(/*inPeer=*/false, /*isPrivateUser=*/false);
	assert(globalLinks.mediaFilter == SearchMediaFilter::Links);
	assert(globalLinks.datePreset == SearchDatePreset::Last30Days);
}

static void TestClientSideMatching() {
	SearchExtraFilters f;
	SearchDateBounds bounds{ .minDate = 1000, .maxDate = 2000 };

	assert(MatchesClientSideFilters(f, bounds, true, 1000));
	assert(MatchesClientSideFilters(f, bounds, false, 1999));
	assert(!MatchesClientSideFilters(f, bounds, true, 999));
	assert(!MatchesClientSideFilters(f, bounds, true, 2000));

	f.senderFilter = SearchSenderFilter::MyMessages;
	assert(MatchesClientSideFilters(f, bounds, true, 1500));
	assert(!MatchesClientSideFilters(f, bounds, false, 1500));

	f.senderFilter = SearchSenderFilter::PeerMessages;
	assert(!MatchesClientSideFilters(f, bounds, true, 1500));
	assert(MatchesClientSideFilters(f, bounds, false, 1500));

	// Open upper bound (maxDate == 0).
	SearchDateBounds openUpper{ .minDate = 1000, .maxDate = 0 };
	assert(MatchesClientSideFilters(f, openUpper, false, 9999999));
	assert(!MatchesClientSideFilters(f, openUpper, false, 999));
}

static void TestPaginationComputation() {
	const auto newestFirstStart = ComputeSearchPagination(
		SearchSortOrder::NewestFirst, 0, 50);
	assert(newestFirstStart.offsetId == 0);
	assert(newestFirstStart.addOffset == 0);

	const auto newestFirstNext = ComputeSearchPagination(
		SearchSortOrder::NewestFirst, 420, 50);
	assert(newestFirstNext.offsetId == 420);
	assert(newestFirstNext.addOffset == 0);

	const auto oldestFirstStart = ComputeSearchPagination(
		SearchSortOrder::OldestFirst, 0, 50);
	assert(oldestFirstStart.offsetId == 1);
	assert(oldestFirstStart.addOffset == -50);

	const auto oldestFirstNext = ComputeSearchPagination(
		SearchSortOrder::OldestFirst, 120, 50);
	assert(oldestFirstNext.offsetId == 121);
	assert(oldestFirstNext.addOffset == -50);
}

static void TestOrderingAndComparison() {
	SearchExtraFilters a;
	SearchExtraFilters b;
	assert(a == b);
	b.sortOrder = SearchSortOrder::OldestFirst;
	assert(a != b);
	assert(a < b);
}

enum class MockServerMode {
	ProperReverse,
	IgnoreNegativeAddOffset,
	CappedReverseMiddle,
	EmptyOnNegativeAddOffset,
};

struct MockResponse {
	std::vector<std::int64_t> rawIds;
	int total = 0;
	bool exhausted = false;
};

static MockResponse QueryMockServer(
		const std::vector<std::int64_t> &allAsc,
		MockServerMode mode,
		const OldestSearchStep &step) {
	// Telegram returns messages in descending ID order.
	auto desc = allAsc;
	std::reverse(desc.begin(), desc.end());
	const auto total = int(desc.size());
	if (step.addOffset < 0) {
		if (mode == MockServerMode::EmptyOnNegativeAddOffset) {
			return { {}, 0, true };
		}
		if (mode == MockServerMode::IgnoreNegativeAddOffset) {
			// Server ignores negative add_offset and returns newest `limit`.
			const auto n = std::min(total, int(step.limit));
			return {
				std::vector<std::int64_t>(desc.begin(), desc.begin() + n),
				total,
				(n == total),
			};
		}
		if (mode == MockServerMode::CappedReverseMiddle) {
			// Server only scans the newest 200 messages when add_offset < 0.
			const auto cap = std::min(total, 200);
			auto sub = std::vector<std::int64_t>(desc.begin(), desc.begin() + cap);
			// Find first index with id < offsetId.
			auto idx = int(sub.size());
			for (auto i = 0; i < int(sub.size()); ++i) {
				if (sub[i] < step.offsetId) {
					idx = i;
					break;
				}
			}
			const auto start = std::max(0, idx + int(step.addOffset));
			const auto end = std::min(int(sub.size()), start + int(step.limit));
			return {
				(start < end)
					? std::vector<std::int64_t>(sub.begin() + start, sub.begin() + end)
					: std::vector<std::int64_t>(),
				total,
				false,
			};
		}
		// ProperReverse
		auto idx = total;
		for (auto i = 0; i < total; ++i) {
			if (desc[i] < step.offsetId) {
				idx = i;
				break;
			}
		}
		const auto start = std::max(0, idx + int(step.addOffset));
		const auto end = std::min(total, start + int(step.limit));
		const auto out = (start < end)
			? std::vector<std::int64_t>(desc.begin() + start, desc.begin() + end)
			: std::vector<std::int64_t>();
		return { out, total, (int(out.size()) == total) };
	}
	// Standard add_offset == 0: messages with id < offsetId (or from newest if 0).
	auto start = 0;
	if (step.offsetId > 0) {
		start = total;
		for (auto i = 0; i < total; ++i) {
			if (desc[i] < step.offsetId) {
				start = i;
				break;
			}
		}
	}
	const auto end = std::min(total, start + int(step.limit));
	const auto out = (start < end)
		? std::vector<std::int64_t>(desc.begin() + start, desc.begin() + end)
		: std::vector<std::int64_t>();
	return { out, total, (start == 0 && end == total) };
}

static std::vector<std::int64_t> RunOldestPlanner(
		const std::vector<std::int64_t> &allAsc,
		MockServerMode mode,
		bool hasDateBounds,
		bool hasTextOrFrom,
		int *rpcCountOut = nullptr,
		int maxPagesToCollect = 1000,
		bool onlyEveryFifth = false) {
	OldestSearchState state;
	state.start(hasDateBounds, hasTextOrFrom, 50);
	auto emitted = std::vector<std::int64_t>();
	auto rpcs = 0;
	auto pages = 0;

	while (!state.exhausted() && pages < maxPagesToCollect) {
		if (state.hasBufferedPage()) {
			const auto res = state.takeBufferedPage();
			emitted.insert(emitted.end(), res.emitIds.begin(), res.emitIds.end());
			++pages;
			continue;
		}
		const auto step = state.nextRequest();
		const auto resp = QueryMockServer(allAsc, mode, step);
		++rpcs;
		assert(rpcs < 500);
		auto filtered = std::vector<std::int64_t>();
		for (const auto id : resp.rawIds) {
			if (!onlyEveryFifth || (id % 5 == 0)) {
				filtered.push_back(id);
			}
		}
		const auto res = state.onPageReceived(
			resp.rawIds,
			filtered,
			resp.total,
			resp.exhausted);
		if (res.action == OldestSearchAction::EmitPage) {
			emitted.insert(emitted.end(), res.emitIds.begin(), res.emitIds.end());
			++pages;
		} else if (res.action == OldestSearchAction::Done) {
			break;
		}
	}
	if (rpcCountOut) {
		*rpcCountOut = rpcs;
	}
	return emitted;
}

static void TestOldestSearchPlanner() {
	// 1. Small chat (20 messages) completes in 1 RPC.
	{
		auto ids = std::vector<std::int64_t>();
		for (int i = 1; i <= 20; ++i) ids.push_back(i * 10);
		auto rpcs = 0;
		const auto got = RunOldestPlanner(ids, MockServerMode::ProperReverse, false, true, &rpcs);
		assert(got == ids);
		assert(rpcs == 1);
	}

	// 2. Proper reverse on 180 messages: 2 RPCs for page 1 (fetch + verify), then fast reverse.
	{
		auto ids = std::vector<std::int64_t>();
		for (int i = 1; i <= 180; ++i) ids.push_back(i * 3);
		auto rpcs = 0;
		const auto got = RunOldestPlanner(ids, MockServerMode::ProperReverse, false, false, &rpcs);
		assert(got == ids);
		assert(rpcs <= 6);
	}

	// 3. Server ignores negative add_offset (350 matches): collects all 350 oldest-first.
	{
		auto ids = std::vector<std::int64_t>();
		for (int i = 1; i <= 350; ++i) ids.push_back(i * 7);
		const auto got = RunOldestPlanner(ids, MockServerMode::IgnoreNegativeAddOffset, false, true);
		assert(got == ids);
	}

	// 4. Server caps negative add_offset to newest 200 out of 500: recovers ALL 500 oldest-first!
	{
		auto ids = std::vector<std::int64_t>();
		for (int i = 1; i <= 500; ++i) ids.push_back(i * 5);
		const auto got = RunOldestPlanner(ids, MockServerMode::CappedReverseMiddle, false, true);
		assert(got == ids);
	}

	// 5. Server returns empty for negative add_offset on text query: falls back cleanly.
	{
		auto ids = std::vector<std::int64_t>();
		for (int i = 1; i <= 120; ++i) ids.push_back(i * 11);
		const auto got = RunOldestPlanner(ids, MockServerMode::EmptyOnNegativeAddOffset, false, true);
		assert(got == ids);
	}

	// 6. Date bounds active: skips negative add_offset immediately.
	{
		auto ids = std::vector<std::int64_t>();
		for (int i = 1; i <= 140; ++i) ids.push_back(1000 + i * 2);
		const auto got = RunOldestPlanner(ids, MockServerMode::EmptyOnNegativeAddOffset, true, true);
		assert(got == ids);
	}

	// 7. Giant 500,000-ID chat with 3,500 matches where server ignores negative add_offset:
	// Binary search finds the very first match (id == 42) in <= 18 RPCs and pages forward with zero gaps!
	{
		auto ids = std::vector<std::int64_t>();
		for (int i = 0; i < 3500; ++i) {
			ids.push_back(42 + std::int64_t(i) * 140);
		}
		auto page1Rpcs = 0;
		const auto firstPage = RunOldestPlanner(
			ids,
			MockServerMode::IgnoreNegativeAddOffset,
			false,
			true,
			&page1Rpcs,
			/*maxPagesToCollect=*/1);
		assert(!firstPage.empty() && firstPage.size() <= 50);
		assert(firstPage.front() == 42);
		for (std::size_t i = 0; i < firstPage.size(); ++i) {
			assert(firstPage[i] == ids[i]);
		}
		assert(page1Rpcs <= 20);

		// Continue through the first 15 pages and confirm strict gapless ordering.
		const auto manyPages = RunOldestPlanner(
			ids,
			MockServerMode::IgnoreNegativeAddOffset,
			false,
			true,
			nullptr,
			/*maxPagesToCollect=*/15);
		assert(manyPages.size() >= 500);
		for (std::size_t i = 0; i < manyPages.size(); ++i) {
			assert(manyPages[i] == ids[i]);
		}
	}

	// 8. Client-side sender filter rejecting 80% of messages while server ignores negative add_offset.
	{
		auto ids = std::vector<std::int64_t>();
		auto expected = std::vector<std::int64_t>();
		for (int i = 1; i <= 250; ++i) {
			ids.push_back(i);
			if (i % 5 == 0) {
				expected.push_back(i);
			}
		}
		const auto got = RunOldestPlanner(
			ids,
			MockServerMode::IgnoreNegativeAddOffset,
			false,
			true,
			nullptr,
			1000,
			/*onlyEveryFifth=*/true);
		assert(got == expected);
	}
}

int main() {
	TestDatePacking();
	TestDefaultAndActiveCount();
	TestNormalizeDates();
	TestSanitizeForContext();
	TestClientSideMatching();
	TestPaginationComputation();
	TestOrderingAndComparison();
	TestOldestSearchPlanner();
	std::cout << "All search filter core, sanitization, date-bound, matching, pagination, and oldest-seek checks passed.\n";
	return 0;
}
