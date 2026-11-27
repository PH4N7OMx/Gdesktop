#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>
#include <utility>
#include <vector>

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

struct OldestSearchStep {
	std::int32_t offsetId = 0;
	std::int32_t addOffset = 0;
	std::int32_t limit = 50;
};

enum class OldestSearchAction : unsigned char {
	EmitPage,
	FetchMore,
	Done,
};

struct OldestSearchResult {
	OldestSearchAction action = OldestSearchAction::Done;
	std::vector<std::int64_t> emitIds;
	int totalCount = 0;
	bool exhausted = false;
};

class OldestSearchState final {
public:
	static constexpr std::int32_t kDefaultPageSize = 50;
	static constexpr std::int32_t kSeekPageLimit = 100;
	static constexpr int kMaxLinearCollectTotal = 2000;
	static constexpr int kMaxLinearCollectPages = 20;
	static constexpr int kMaxBinarySteps = 20;
	static constexpr int kMaxForwardWalkPages = 8;

	void reset() {
		*this = OldestSearchState{};
	}

	void start(
			bool hasDateBounds,
			bool hasTextOrFrom,
			std::int32_t pageSize = kDefaultPageSize) {
		reset();
		_pageSize = std::clamp(pageSize, std::int32_t(1), kSeekPageLimit);
		_hasTextOrFrom = hasTextOrFrom;
		if (hasDateBounds) {
			_phase = Phase::CollectOlder;
			_cursorId = 0;
			_reachedNewest = true;
		} else {
			_phase = Phase::FastReverse;
			_reachedNewest = false;
		}
	}

	[[nodiscard]] bool active() const noexcept {
		return (_phase != Phase::Idle);
	}

	[[nodiscard]] bool exhausted() const noexcept {
		return (_phase == Phase::Exhausted);
	}

	[[nodiscard]] bool hasEmittedAny() const noexcept {
		return _emittedAny;
	}

	[[nodiscard]] bool hasBufferedPage() const noexcept {
		return (_phase == Phase::DrainBuffer) && (_readyIndex < _readyIds.size());
	}

	[[nodiscard]] OldestSearchResult takeBufferedPage() {
		if (!hasBufferedPage()) {
			return {
				.action = OldestSearchAction::Done,
				.totalCount = _knownTotal,
				.exhausted = (_phase == Phase::Exhausted),
			};
		}
		const auto remaining = _readyIds.size() - _readyIndex;
		const auto take = std::min(remaining, std::size_t(_pageSize));
		auto page = std::vector<std::int64_t>(
			_readyIds.begin() + _readyIndex,
			_readyIds.begin() + _readyIndex + take);
		_readyIndex += take;
		if (!page.empty()) {
			_lastEmittedId = std::max(_lastEmittedId, page.back());
		}
		_emittedAny = true;
		auto isExhausted = false;
		if (_readyIndex >= _readyIds.size()) {
			_readyIds.clear();
			_readyIndex = 0;
			if (_reachedNewest) {
				_phase = Phase::Exhausted;
				isExhausted = true;
			} else {
				beginForwardWindow();
			}
		}
		return {
			.action = OldestSearchAction::EmitPage,
			.emitIds = std::move(page),
			.totalCount = _knownTotal,
			.exhausted = isExhausted,
		};
	}

	[[nodiscard]] OldestSearchStep nextRequest() const noexcept {
		switch (_phase) {
		case Phase::FastReverse:
			return {
				.offsetId = 1,
				.addOffset = -_pageSize,
				.limit = _pageSize,
			};
		case Phase::VerifyOldest:
			return {
				.offsetId = clampId(_verifyOffsetId),
				.addOffset = 0,
				.limit = kSeekPageLimit,
			};
		case Phase::FallbackStart:
			return {
				.offsetId = 0,
				.addOffset = 0,
				.limit = kSeekPageLimit,
			};
		case Phase::ServerReverse:
			return {
				.offsetId = clampId(std::max(_lastServerReverseId + 1, std::int64_t(1))),
				.addOffset = -_pageSize,
				.limit = _pageSize,
			};
		case Phase::CollectOlder:
			return {
				.offsetId = clampId(_cursorId),
				.addOffset = 0,
				.limit = kSeekPageLimit,
			};
		case Phase::SeekBinary:
			return {
				.offsetId = clampId(_binaryProbeId),
				.addOffset = 0,
				.limit = kSeekPageLimit,
			};
		case Phase::ForwardWindow:
			return {
				.offsetId = clampId(_forwardCursorId),
				.addOffset = 0,
				.limit = kSeekPageLimit,
			};
		case Phase::Idle:
		case Phase::DrainBuffer:
		case Phase::Exhausted:
			return {};
		}
		return {};
	}

	[[nodiscard]] OldestSearchResult onPageReceived(
			const std::vector<std::int64_t> &rawIds,
			const std::vector<std::int64_t> &filteredIds,
			int serverTotalCount,
			bool serverExhausted) {
		const auto rawCount = int(rawIds.size());
		auto minRawId = std::int64_t(0);
		auto maxRawId = std::int64_t(0);
		for (const auto id : rawIds) {
			if (id <= 0) {
				continue;
			}
			if (minRawId == 0 || id < minRawId) {
				minRawId = id;
			}
			if (id > maxRawId) {
				maxRawId = id;
			}
		}
		if (serverTotalCount > _knownTotal) {
			_knownTotal = serverTotalCount;
		}

		switch (_phase) {
		case Phase::FastReverse:
			return handleFastReverse(
				rawCount,
				minRawId,
				maxRawId,
				filteredIds,
				serverExhausted);
		case Phase::VerifyOldest:
			return handleVerifyOldest(
				rawCount,
				minRawId,
				maxRawId,
				filteredIds,
				serverExhausted);
		case Phase::FallbackStart:
			return handleFallbackStart(
				rawCount,
				minRawId,
				maxRawId,
				filteredIds,
				serverExhausted);
		case Phase::ServerReverse:
			return handleServerReverse(
				rawCount,
				maxRawId,
				filteredIds,
				serverExhausted);
		case Phase::CollectOlder:
			return handleCollectOlder(
				rawCount,
				minRawId,
				maxRawId,
				filteredIds,
				serverExhausted);
		case Phase::SeekBinary:
			return handleSeekBinary(
				rawCount,
				minRawId,
				maxRawId,
				filteredIds,
				serverExhausted);
		case Phase::ForwardWindow:
			return handleForwardWindow(
				rawCount,
				minRawId,
				maxRawId,
				filteredIds,
				serverExhausted);
		case Phase::Idle:
		case Phase::DrainBuffer:
		case Phase::Exhausted:
			return {
				.action = OldestSearchAction::Done,
				.totalCount = _knownTotal,
				.exhausted = true,
			};
		}
		return {};
	}

private:
	enum class Phase : unsigned char {
		Idle,
		FastReverse,
		VerifyOldest,
		FallbackStart,
		ServerReverse,
		CollectOlder,
		SeekBinary,
		DrainBuffer,
		ForwardWindow,
		Exhausted,
	};

	[[nodiscard]] static std::int32_t clampId(std::int64_t id) noexcept {
		constexpr auto kMaxMtpId = std::int64_t(0x3FFFFFFF);
		return static_cast<std::int32_t>(std::clamp(id, std::int64_t(0), kMaxMtpId));
	}

	static void sortAndDedupAbove(
			std::vector<std::int64_t> &ids,
			std::int64_t floorExclusive) {
		ids.erase(
			std::remove_if(ids.begin(), ids.end(), [=](std::int64_t id) {
				return id <= floorExclusive;
			}),
			ids.end());
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	}

	void pushAnchor(std::int64_t id) {
		if (id < 0) {
			return;
		}
		if (_upperAnchors.empty() || _upperAnchors.back() != id) {
			_upperAnchors.push_back(id);
		}
	}

	void beginBinarySeek(
			std::int64_t lo,
			std::int64_t hi,
			std::vector<std::int64_t> lowestBatchIds,
			std::int64_t lowestBatchMaxRaw,
			bool lastProbeWasShort) {
		_phase = Phase::SeekBinary;
		_lo = std::max(lo, std::int64_t(1));
		_hi = hi;
		_lowestBatchIds = std::move(lowestBatchIds);
		_lowestBatchMaxRaw = lowestBatchMaxRaw;
		_binarySteps = 0;
		_lastProbeWasShort = lastProbeWasShort;
		_directHiProbed = false;
		if (_lastProbeWasShort && _hi > _lo) {
			_binaryProbeId = _hi;
			_directHiProbed = true;
		} else {
			const auto mid = _lo + (_hi - _lo) / 2;
			_binaryProbeId = (mid > _lo) ? mid : _hi;
			if (_binaryProbeId == _hi) {
				_directHiProbed = true;
			}
		}
	}

	[[nodiscard]] OldestSearchResult finishToBuffer(
			std::vector<std::int64_t> ids,
			std::int64_t maxRawScanned) {
		_forwardFloorId = std::max(_forwardFloorId, maxRawScanned);
		sortAndDedupAbove(ids, _lastEmittedId);
		if (ids.empty()) {
			if (_reachedNewest) {
				_phase = Phase::Exhausted;
				_emittedAny = true;
				return {
					.action = OldestSearchAction::EmitPage,
					.emitIds = {},
					.totalCount = _knownTotal,
					.exhausted = true,
				};
			}
			beginForwardWindow();
			if (_phase == Phase::Exhausted) {
				_emittedAny = true;
				return {
					.action = OldestSearchAction::EmitPage,
					.emitIds = {},
					.totalCount = _knownTotal,
					.exhausted = true,
				};
			}
			return {
				.action = OldestSearchAction::FetchMore,
				.totalCount = _knownTotal,
				.exhausted = false,
			};
		}
		_readyIds = std::move(ids);
		_readyIndex = 0;
		_phase = Phase::DrainBuffer;
		return takeBufferedPage();
	}

	void beginForwardWindow() {
		while (!_upperAnchors.empty()
			&& _upperAnchors.back() > 0
			&& _upperAnchors.back() <= _forwardFloorId) {
			_upperAnchors.pop_back();
		}
		_forwardUpperId = _upperAnchors.empty()
			? std::int64_t(0)
			: _upperAnchors.back();
		_forwardCursorId = _forwardUpperId;
		_forwardWalkPages = 0;
		_forwardWindowMaxRaw = _forwardFloorId;
		_forwardCollectedIds.clear();
		_phase = Phase::ForwardWindow;
	}

	[[nodiscard]] OldestSearchResult handleFastReverse(
			int rawCount,
			std::int64_t minRawId,
			std::int64_t maxRawId,
			const std::vector<std::int64_t> &filteredIds,
			bool serverExhausted) {
		if (rawCount == 0) {
			if (_hasTextOrFrom) {
				_phase = Phase::FallbackStart;
				return {
					.action = OldestSearchAction::FetchMore,
					.totalCount = _knownTotal,
					.exhausted = false,
				};
			}
			_phase = Phase::Exhausted;
			_emittedAny = true;
			return {
				.action = OldestSearchAction::EmitPage,
				.emitIds = {},
				.totalCount = 0,
				.exhausted = true,
			};
		}
		if (serverExhausted) {
			auto page = filteredIds;
			sortAndDedupAbove(page, 0);
			_phase = Phase::Exhausted;
			_emittedAny = true;
			return {
				.action = OldestSearchAction::EmitPage,
				.emitIds = std::move(page),
				.totalCount = std::max(_knownTotal, rawCount),
				.exhausted = true,
			};
		}
		_stashedFastIds = filteredIds;
		_fastMinRawId = minRawId;
		_fastMaxRawId = maxRawId;
		_verifyOffsetId = minRawId;
		_phase = Phase::VerifyOldest;
		return {
			.action = OldestSearchAction::FetchMore,
			.totalCount = _knownTotal,
			.exhausted = false,
		};
	}

	[[nodiscard]] OldestSearchResult handleVerifyOldest(
			int rawCount,
			std::int64_t minRawId,
			std::int64_t maxRawId,
			const std::vector<std::int64_t> &filteredIds,
			bool serverExhausted) {
		if (rawCount == 0) {
			auto page = std::move(_stashedFastIds);
			_stashedFastIds.clear();
			sortAndDedupAbove(page, 0);
			if (!page.empty()) {
				_lastEmittedId = std::max(_lastEmittedId, page.back());
			}
			_lastServerReverseId = _fastMaxRawId;
			_phase = Phase::ServerReverse;
			_emittedAny = true;
			return {
				.action = OldestSearchAction::EmitPage,
				.emitIds = std::move(page),
				.totalCount = _knownTotal,
				.exhausted = false,
			};
		}

		_collectedIds = std::move(_stashedFastIds);
		_stashedFastIds.clear();
		_collectedIds.insert(
			_collectedIds.end(),
			filteredIds.begin(),
			filteredIds.end());
		_maxCollectedRawId = std::max(_fastMaxRawId, maxRawId);

		if (serverExhausted) {
			return finishToBuffer(std::move(_collectedIds), _maxCollectedRawId);
		}
		if (_knownTotal > kMaxLinearCollectTotal) {
			_collectedIds.clear();
			_upperAnchors.clear();
			pushAnchor(_fastMinRawId);
			beginBinarySeek(
				1,
				minRawId,
				filteredIds,
				maxRawId,
				rawCount < kSeekPageLimit);
			return {
				.action = OldestSearchAction::FetchMore,
				.totalCount = _knownTotal,
				.exhausted = false,
			};
		}
		_cursorId = minRawId;
		_collectPages = 1;
		_phase = Phase::CollectOlder;
		return {
			.action = OldestSearchAction::FetchMore,
			.totalCount = _knownTotal,
			.exhausted = false,
		};
	}

	[[nodiscard]] OldestSearchResult handleFallbackStart(
			int rawCount,
			std::int64_t minRawId,
			std::int64_t maxRawId,
			const std::vector<std::int64_t> &filteredIds,
			bool serverExhausted) {
		_reachedNewest = true;
		if (rawCount == 0) {
			_phase = Phase::Exhausted;
			_emittedAny = true;
			return {
				.action = OldestSearchAction::EmitPage,
				.emitIds = {},
				.totalCount = 0,
				.exhausted = true,
			};
		}
		_collectedIds = filteredIds;
		_maxCollectedRawId = maxRawId;
		if (serverExhausted) {
			return finishToBuffer(std::move(_collectedIds), _maxCollectedRawId);
		}
		if (_knownTotal > kMaxLinearCollectTotal) {
			_collectedIds.clear();
			_reachedNewest = false;
			_upperAnchors.clear();
			pushAnchor(0);
			beginBinarySeek(
				1,
				minRawId,
				filteredIds,
				maxRawId,
				rawCount < kSeekPageLimit);
			return {
				.action = OldestSearchAction::FetchMore,
				.totalCount = _knownTotal,
				.exhausted = false,
			};
		}
		_cursorId = minRawId;
		_collectPages = 1;
		_phase = Phase::CollectOlder;
		return {
			.action = OldestSearchAction::FetchMore,
			.totalCount = _knownTotal,
			.exhausted = false,
		};
	}

	[[nodiscard]] OldestSearchResult handleServerReverse(
			int rawCount,
			std::int64_t maxRawId,
			const std::vector<std::int64_t> &filteredIds,
			bool serverExhausted) {
		if (rawCount == 0 || maxRawId <= _lastServerReverseId) {
			_phase = Phase::Exhausted;
			_emittedAny = true;
			return {
				.action = OldestSearchAction::EmitPage,
				.emitIds = {},
				.totalCount = _knownTotal,
				.exhausted = true,
			};
		}
		auto page = filteredIds;
		sortAndDedupAbove(page, _lastServerReverseId);
		_lastServerReverseId = maxRawId;
		if (!page.empty()) {
			_lastEmittedId = std::max(_lastEmittedId, page.back());
		}
		if (serverExhausted) {
			_phase = Phase::Exhausted;
		}
		_emittedAny = true;
		return {
			.action = OldestSearchAction::EmitPage,
			.emitIds = std::move(page),
			.totalCount = _knownTotal,
			.exhausted = serverExhausted,
		};
	}

	[[nodiscard]] OldestSearchResult handleCollectOlder(
			int rawCount,
			std::int64_t minRawId,
			std::int64_t maxRawId,
			const std::vector<std::int64_t> &filteredIds,
			bool serverExhausted) {
		_collectedIds.insert(
			_collectedIds.end(),
			filteredIds.begin(),
			filteredIds.end());
		_maxCollectedRawId = std::max(_maxCollectedRawId, maxRawId);
		++_collectPages;

		if (rawCount == 0
			|| serverExhausted
			|| (_cursorId > 0 && minRawId >= _cursorId)) {
			return finishToBuffer(std::move(_collectedIds), _maxCollectedRawId);
		}
		_cursorId = minRawId;
		if (_collectPages >= kMaxLinearCollectPages) {
			const auto wasReachedNewest = _reachedNewest;
			_collectedIds.clear();
			_reachedNewest = false;
			_upperAnchors.clear();
			if (wasReachedNewest) {
				pushAnchor(0);
			}
			pushAnchor(_maxCollectedRawId + 1);
			beginBinarySeek(
				1,
				minRawId,
				filteredIds,
				maxRawId,
				rawCount < kSeekPageLimit);
			return {
				.action = OldestSearchAction::FetchMore,
				.totalCount = _knownTotal,
				.exhausted = false,
			};
		}
		return {
			.action = OldestSearchAction::FetchMore,
			.totalCount = _knownTotal,
			.exhausted = false,
		};
	}

	[[nodiscard]] OldestSearchResult handleSeekBinary(
			int rawCount,
			std::int64_t minRawId,
			std::int64_t maxRawId,
			const std::vector<std::int64_t> &filteredIds,
			bool serverExhausted) {
		++_binarySteps;
		const auto hasAnyInRange = (rawCount > 0) && (maxRawId >= _lo);
		if (!hasAnyInRange) {
			if (_binaryProbeId >= _hi) {
				auto batch = std::move(_lowestBatchIds);
				_lowestBatchIds.clear();
				return finishToBuffer(std::move(batch), _lowestBatchMaxRaw);
			}
			_lo = std::max(_lo, _binaryProbeId);
		} else {
			if (minRawId < _hi) {
				pushAnchor(_hi);
				_hi = std::max(minRawId, _lo);
				_lowestBatchIds = filteredIds;
				_lowestBatchMaxRaw = maxRawId;
				_lastProbeWasShort = (rawCount < kSeekPageLimit);
				_directHiProbed = false;
			}
			if (serverExhausted || minRawId <= _lo || _hi <= _lo) {
				auto batch = std::move(_lowestBatchIds);
				_lowestBatchIds.clear();
				return finishToBuffer(std::move(batch), _lowestBatchMaxRaw);
			}
		}

		if (_binarySteps >= kMaxBinarySteps || _hi <= _lo) {
			auto batch = std::move(_lowestBatchIds);
			_lowestBatchIds.clear();
			return finishToBuffer(std::move(batch), _lowestBatchMaxRaw);
		}
		if (_lastProbeWasShort && !_directHiProbed) {
			_binaryProbeId = _hi;
			_directHiProbed = true;
			return {
				.action = OldestSearchAction::FetchMore,
				.totalCount = _knownTotal,
				.exhausted = false,
			};
		}
		const auto mid = _lo + (_hi - _lo) / 2;
		if (mid <= _lo) {
			if (!_directHiProbed && _hi > _lo) {
				_binaryProbeId = _hi;
				_directHiProbed = true;
				return {
					.action = OldestSearchAction::FetchMore,
					.totalCount = _knownTotal,
					.exhausted = false,
				};
			}
			auto batch = std::move(_lowestBatchIds);
			_lowestBatchIds.clear();
			return finishToBuffer(std::move(batch), _lowestBatchMaxRaw);
		}
		_binaryProbeId = mid;
		return {
			.action = OldestSearchAction::FetchMore,
			.totalCount = _knownTotal,
			.exhausted = false,
		};
	}

	[[nodiscard]] OldestSearchResult handleForwardWindow(
			int rawCount,
			std::int64_t minRawId,
			std::int64_t maxRawId,
			const std::vector<std::int64_t> &filteredIds,
			bool serverExhausted) {
		++_forwardWalkPages;
		for (const auto id : filteredIds) {
			if (id > _forwardFloorId) {
				_forwardCollectedIds.push_back(id);
			}
		}
		if (maxRawId > _forwardWindowMaxRaw) {
			_forwardWindowMaxRaw = maxRawId;
		}
		const auto hitFloor = (rawCount == 0)
			|| serverExhausted
			|| (minRawId <= _forwardFloorId)
			|| (_forwardCursorId > 0 && minRawId >= _forwardCursorId);
		if (hitFloor) {
			if (_forwardUpperId == 0) {
				_reachedNewest = true;
			} else if (!_upperAnchors.empty()
				&& _upperAnchors.back() == _forwardUpperId) {
				_upperAnchors.pop_back();
			}
			const auto nextFloor = (_forwardUpperId > 0)
				? std::max(_forwardWindowMaxRaw, _forwardUpperId - 1)
				: _forwardWindowMaxRaw;
			auto collected = std::move(_forwardCollectedIds);
			_forwardCollectedIds.clear();
			return finishToBuffer(std::move(collected), nextFloor);
		}
		if (_forwardWalkPages >= kMaxForwardWalkPages) {
			pushAnchor(minRawId);
			_forwardCollectedIds.clear();
			beginBinarySeek(
				_forwardFloorId + 1,
				minRawId,
				filteredIds,
				maxRawId,
				rawCount < kSeekPageLimit);
			return {
				.action = OldestSearchAction::FetchMore,
				.totalCount = _knownTotal,
				.exhausted = false,
			};
		}
		_forwardCursorId = minRawId;
		return {
			.action = OldestSearchAction::FetchMore,
			.totalCount = _knownTotal,
			.exhausted = false,
		};
	}

	Phase _phase = Phase::Idle;
	std::int32_t _pageSize = kDefaultPageSize;
	bool _hasTextOrFrom = false;
	bool _reachedNewest = false;
	bool _emittedAny = false;
	int _knownTotal = 0;

	std::int64_t _lastEmittedId = 0;
	std::int64_t _lastServerReverseId = 0;

	std::vector<std::int64_t> _stashedFastIds;
	std::int64_t _fastMinRawId = 0;
	std::int64_t _fastMaxRawId = 0;
	std::int64_t _verifyOffsetId = 0;

	std::vector<std::int64_t> _collectedIds;
	std::int64_t _cursorId = 0;
	std::int64_t _maxCollectedRawId = 0;
	int _collectPages = 0;

	std::int64_t _lo = 1;
	std::int64_t _hi = 0;
	std::int64_t _binaryProbeId = 0;
	int _binarySteps = 0;
	bool _lastProbeWasShort = false;
	bool _directHiProbed = false;
	std::vector<std::int64_t> _lowestBatchIds;
	std::int64_t _lowestBatchMaxRaw = 0;

	std::vector<std::int64_t> _upperAnchors;
	std::int64_t _forwardFloorId = 0;
	std::int64_t _forwardUpperId = 0;
	std::int64_t _forwardCursorId = 0;
	std::int64_t _forwardWindowMaxRaw = 0;
	int _forwardWalkPages = 0;
	std::vector<std::int64_t> _forwardCollectedIds;

	std::vector<std::int64_t> _readyIds;
	std::size_t _readyIndex = 0;
};

} // namespace Jel
