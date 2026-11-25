#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>
#include <utility>

namespace Jel {

enum class SearchSortOrder : unsigned char {
	NewestFirst,
	OldestFirst,
};

enum class SearchMediaFilter : unsigned char {
	All,
	Photos,
	Videos,
	Files,
	Links,
	Voice,
	Music,
	Gifs,
	Pinned,
	Polls,
};

enum class SearchDatePreset : unsigned char {
	AnyTime,
	Today,
	Last7Days,
	Last30Days,
	LastYear,
	CustomRange,
};

enum class SearchSenderFilter : unsigned char {
	All,
	MyMessages,
	PeerMessages,
};

struct SearchDateBounds {
	std::int32_t minDate = 0;
	std::int32_t maxDate = 0;

	[[nodiscard]] bool empty() const noexcept {
		return (minDate == 0) && (maxDate == 0);
	}

	friend inline auto operator<=>(
		const SearchDateBounds &,
		const SearchDateBounds &) noexcept = default;
	friend inline bool operator==(
		const SearchDateBounds &,
		const SearchDateBounds &) noexcept = default;
};

[[nodiscard]] inline std::int32_t PackSearchDate(
		int year,
		int month,
		int day) noexcept {
	if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31) {
		return 0;
	}
	return year * 10000 + month * 100 + day;
}

[[nodiscard]] inline bool IsValidPackedSearchDate(std::int32_t packed) noexcept {
	if (packed <= 0) {
		return false;
	}
	const auto year = packed / 10000;
	const auto month = (packed / 100) % 100;
	const auto day = packed % 100;
	return (year >= 1970 && year <= 9999)
		&& (month >= 1 && month <= 12)
		&& (day >= 1 && day <= 31);
}

struct SearchExtraFilters {
	SearchSortOrder sortOrder = SearchSortOrder::NewestFirst;
	SearchMediaFilter mediaFilter = SearchMediaFilter::All;
	SearchDatePreset datePreset = SearchDatePreset::AnyTime;
	std::int32_t customFromDate = 0; // YYYYMMDD
	std::int32_t customToDate = 0;   // YYYYMMDD
	SearchSenderFilter senderFilter = SearchSenderFilter::All;

	[[nodiscard]] bool hasDateFilter() const noexcept {
		if (datePreset == SearchDatePreset::AnyTime) {
			return false;
		}
		if (datePreset == SearchDatePreset::CustomRange) {
			return IsValidPackedSearchDate(customFromDate)
				|| IsValidPackedSearchDate(customToDate);
		}
		return true;
	}

	[[nodiscard]] bool isDefault() const noexcept {
		return (sortOrder == SearchSortOrder::NewestFirst)
			&& (mediaFilter == SearchMediaFilter::All)
			&& !hasDateFilter()
			&& (senderFilter == SearchSenderFilter::All);
	}

	[[nodiscard]] int activeCount() const noexcept {
		auto count = 0;
		if (sortOrder != SearchSortOrder::NewestFirst) {
			++count;
		}
		if (mediaFilter != SearchMediaFilter::All) {
			++count;
		}
		if (hasDateFilter()) {
			++count;
		}
		if (senderFilter != SearchSenderFilter::All) {
			++count;
		}
		return count;
	}

	[[nodiscard]] bool triggersEmptyQuerySearch() const noexcept {
		return (mediaFilter != SearchMediaFilter::All);
	}

	[[nodiscard]] bool isOldestFirst() const noexcept {
		return (sortOrder == SearchSortOrder::OldestFirst);
	}

	[[nodiscard]] bool hasClientSideSenderFilter() const noexcept {
		return (senderFilter != SearchSenderFilter::All);
	}

	void reset() noexcept {
		*this = SearchExtraFilters{};
	}

	void normalizeDates() noexcept {
		if (datePreset != SearchDatePreset::CustomRange) {
			customFromDate = 0;
			customToDate = 0;
			return;
		}
		const auto fromValid = IsValidPackedSearchDate(customFromDate);
		const auto toValid = IsValidPackedSearchDate(customToDate);
		if (!fromValid && !toValid) {
			datePreset = SearchDatePreset::AnyTime;
			customFromDate = 0;
			customToDate = 0;
			return;
		}
		if (!fromValid) {
			customFromDate = customToDate;
		} else if (!toValid) {
			customToDate = customFromDate;
		} else if (customFromDate > customToDate) {
			std::swap(customFromDate, customToDate);
		}
	}

	void sanitizeForContext(bool inPeer, bool isPrivateUser) noexcept {
		normalizeDates();
		if (!inPeer) {
			sortOrder = SearchSortOrder::NewestFirst;
			senderFilter = SearchSenderFilter::All;
			if (mediaFilter == SearchMediaFilter::Pinned
				|| mediaFilter == SearchMediaFilter::Polls) {
				mediaFilter = SearchMediaFilter::All;
			}
		} else if (!isPrivateUser) {
			senderFilter = SearchSenderFilter::All;
		}
	}

	friend inline auto operator<=>(
		const SearchExtraFilters &,
		const SearchExtraFilters &) noexcept = default;
	friend inline bool operator==(
		const SearchExtraFilters &,
		const SearchExtraFilters &) noexcept = default;
};

[[nodiscard]] inline bool MatchesClientSideFilters(
		const SearchExtraFilters &filters,
		const SearchDateBounds &bounds,
		bool isOutgoing,
		std::int32_t messageDate) noexcept {
	switch (filters.senderFilter) {
	case SearchSenderFilter::All:
		break;
	case SearchSenderFilter::MyMessages:
		if (!isOutgoing) {
			return false;
		}
		break;
	case SearchSenderFilter::PeerMessages:
		if (isOutgoing) {
			return false;
		}
		break;
	}
	if (bounds.minDate > 0 && messageDate < bounds.minDate) {
		return false;
	}
	if (bounds.maxDate > 0 && messageDate >= bounds.maxDate) {
		return false;
	}
	return true;
}

struct SearchPaginationParams {
	std::int32_t offsetId = 0;
	std::int32_t addOffset = 0;
};

[[nodiscard]] inline SearchPaginationParams ComputeSearchPagination(
		SearchSortOrder order,
		std::int64_t lastLoadedId,
		std::int32_t limit) noexcept {
	constexpr auto kMaxMtpId = std::int64_t(0x3FFFFFFF);
	if (order == SearchSortOrder::OldestFirst) {
		const auto nextOffset = (lastLoadedId > 0)
			? std::clamp(lastLoadedId + 1, std::int64_t(1), kMaxMtpId)
			: std::int64_t(1);
		return {
			.offsetId = static_cast<std::int32_t>(nextOffset),
			.addOffset = -limit,
		};
	}
	const auto clamped = std::clamp(lastLoadedId, std::int64_t(0), kMaxMtpId);
	return {
		.offsetId = static_cast<std::int32_t>(clamped),
		.addOffset = 0,
	};
}

} // namespace Jel
