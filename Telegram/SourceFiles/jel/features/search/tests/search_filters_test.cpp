#include "search_filters_core.h"

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

int main() {
	TestDatePacking();
	TestDefaultAndActiveCount();
	TestNormalizeDates();
	TestSanitizeForContext();
	TestClientSideMatching();
	TestPaginationComputation();
	TestOrderingAndComparison();
	std::cout << "All search filter core, sanitization, date-bound, matching, and pagination checks passed.\n";
	return 0;
}
