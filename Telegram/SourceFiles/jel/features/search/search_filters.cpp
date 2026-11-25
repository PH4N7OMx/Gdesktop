#include "jel/features/search/search_filters.h"

#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "scheme.h"

#include <QtCore/QDateTime>
#include <QtCore/QTime>

namespace Jel {
namespace {

[[nodiscard]] std::int32_t StartOfDayUnix(QDate date) {
	if (!date.isValid()) {
		return 0;
	}
	const auto ts = base::unixtime::serialize(QDateTime(date, QTime(0, 0)));
	return (ts > 0) ? ts : 0;
}

[[nodiscard]] QString FormatShortDate(QDate date) {
	if (!date.isValid()) {
		return QString();
	}
	const auto currentYear = QDate::currentDate().year();
	return (date.year() == currentYear)
		? date.toString(u"dd.MM"_q)
		: date.toString(u"dd.MM.yyyy"_q);
}

} // namespace

std::int32_t PackSearchDate(QDate date) {
	if (!date.isValid()) {
		return 0;
	}
	return PackSearchDate(date.year(), date.month(), date.day());
}

QDate UnpackSearchDate(std::int32_t packed) {
	if (!IsValidPackedSearchDate(packed)) {
		return QDate();
	}
	const auto year = packed / 10000;
	const auto month = (packed / 100) % 100;
	const auto day = packed % 100;
	return QDate(year, month, day);
}

SearchDateBounds ResolveSearchDateBounds(
		const SearchExtraFilters &filters,
		QDate today) {
	if (!today.isValid()) {
		today = QDate::currentDate();
	}
	switch (filters.datePreset) {
	case SearchDatePreset::AnyTime:
		return {};
	case SearchDatePreset::Today:
		return {
			.minDate = StartOfDayUnix(today),
			.maxDate = 0,
		};
	case SearchDatePreset::Last7Days:
		return {
			.minDate = StartOfDayUnix(today.addDays(-6)),
			.maxDate = 0,
		};
	case SearchDatePreset::Last30Days:
		return {
			.minDate = StartOfDayUnix(today.addDays(-29)),
			.maxDate = 0,
		};
	case SearchDatePreset::LastYear:
		return {
			.minDate = StartOfDayUnix(today.addYears(-1)),
			.maxDate = 0,
		};
	case SearchDatePreset::CustomRange: {
		auto from = UnpackSearchDate(filters.customFromDate);
		auto to = UnpackSearchDate(filters.customToDate);
		if (!from.isValid() && !to.isValid()) {
			return {};
		}
		if (!from.isValid()) {
			from = to;
		} else if (!to.isValid()) {
			to = from;
		} else if (from > to) {
			std::swap(from, to);
		}
		return {
			.minDate = StartOfDayUnix(from),
			.maxDate = StartOfDayUnix(to.addDays(1)),
		};
	}
	}
	return {};
}

MTPMessagesFilter SearchMediaFilterToMTP(SearchMediaFilter filter) {
	switch (filter) {
	case SearchMediaFilter::All:
		return MTP_inputMessagesFilterEmpty();
	case SearchMediaFilter::Photos:
		return MTP_inputMessagesFilterPhotos();
	case SearchMediaFilter::Videos:
		return MTP_inputMessagesFilterVideo();
	case SearchMediaFilter::Files:
		return MTP_inputMessagesFilterDocument();
	case SearchMediaFilter::Links:
		return MTP_inputMessagesFilterUrl();
	case SearchMediaFilter::Voice:
		return MTP_inputMessagesFilterRoundVoice();
	case SearchMediaFilter::Music:
		return MTP_inputMessagesFilterMusic();
	case SearchMediaFilter::Gifs:
		return MTP_inputMessagesFilterGif();
	case SearchMediaFilter::Pinned:
		return MTP_inputMessagesFilterPinned();
	case SearchMediaFilter::Polls:
		return MTP_inputMessagesFilterPoll();
	}
	return MTP_inputMessagesFilterEmpty();
}

QString SearchExtraFiltersToken(const SearchExtraFilters &filters) {
	if (filters.isDefault()) {
		return QString();
	}
	return u"\ns:%1;m:%2;d:%3;df:%4;dt:%5;u:%6"_q
		.arg(int(filters.sortOrder))
		.arg(int(filters.mediaFilter))
		.arg(int(filters.datePreset))
		.arg(filters.customFromDate)
		.arg(filters.customToDate)
		.arg(int(filters.senderFilter));
}

QString SearchMediaFilterLabel(SearchMediaFilter filter) {
	switch (filter) {
	case SearchMediaFilter::All:
		return tr::jel_SearchFilterMediaAll(tr::now);
	case SearchMediaFilter::Photos:
		return tr::jel_SearchFilterMediaPhotos(tr::now);
	case SearchMediaFilter::Videos:
		return tr::jel_SearchFilterMediaVideos(tr::now);
	case SearchMediaFilter::Files:
		return tr::jel_SearchFilterMediaFiles(tr::now);
	case SearchMediaFilter::Links:
		return tr::jel_SearchFilterMediaLinks(tr::now);
	case SearchMediaFilter::Voice:
		return tr::jel_SearchFilterMediaVoice(tr::now);
	case SearchMediaFilter::Music:
		return tr::jel_SearchFilterMediaMusic(tr::now);
	case SearchMediaFilter::Gifs:
		return tr::jel_SearchFilterMediaGifs(tr::now);
	case SearchMediaFilter::Pinned:
		return tr::jel_SearchFilterMediaPinned(tr::now);
	case SearchMediaFilter::Polls:
		return tr::jel_SearchFilterMediaPolls(tr::now);
	}
	return tr::jel_SearchFilterMediaAll(tr::now);
}

QString SearchDateFilterLabel(const SearchExtraFilters &filters) {
	switch (filters.datePreset) {
	case SearchDatePreset::AnyTime:
		return tr::jel_SearchFilterDateAny(tr::now);
	case SearchDatePreset::Today:
		return tr::jel_SearchFilterDateToday(tr::now);
	case SearchDatePreset::Last7Days:
		return tr::jel_SearchFilterDate7Days(tr::now);
	case SearchDatePreset::Last30Days:
		return tr::jel_SearchFilterDate30Days(tr::now);
	case SearchDatePreset::LastYear:
		return tr::jel_SearchFilterDateYear(tr::now);
	case SearchDatePreset::CustomRange: {
		auto from = UnpackSearchDate(filters.customFromDate);
		auto to = UnpackSearchDate(filters.customToDate);
		if (!from.isValid() && !to.isValid()) {
			return tr::jel_SearchFilterDateAny(tr::now);
		}
		if (!from.isValid()) {
			from = to;
		} else if (!to.isValid()) {
			to = from;
		} else if (from > to) {
			std::swap(from, to);
		}
		if (from == to) {
			return FormatShortDate(from);
		}
		return FormatShortDate(from) + QString::fromUtf8(" \xE2\x80\x93 ") + FormatShortDate(to);
	}
	}
	return tr::jel_SearchFilterDateAny(tr::now);
}

QString SearchSenderFilterLabel(SearchSenderFilter filter) {
	switch (filter) {
	case SearchSenderFilter::All:
		return tr::jel_SearchFilterSenderAll(tr::now);
	case SearchSenderFilter::MyMessages:
		return tr::jel_SearchFilterSenderMine(tr::now);
	case SearchSenderFilter::PeerMessages:
		return tr::jel_SearchFilterSenderPeer(tr::now);
	}
	return tr::jel_SearchFilterSenderAll(tr::now);
}

QString SearchExtraFiltersSummary(const SearchExtraFilters &filters) {
	if (filters.isDefault()) {
		return QString();
	}
	auto parts = QStringList();
	if (filters.sortOrder == SearchSortOrder::OldestFirst) {
		parts.push_back(tr::jel_SearchFilterSortOldest(tr::now));
	}
	if (filters.mediaFilter != SearchMediaFilter::All) {
		parts.push_back(SearchMediaFilterLabel(filters.mediaFilter));
	}
	if (filters.hasDateFilter()) {
		parts.push_back(SearchDateFilterLabel(filters));
	}
	if (filters.senderFilter != SearchSenderFilter::All) {
		parts.push_back(SearchSenderFilterLabel(filters.senderFilter));
	}
	return parts.join(QString::fromUtf8(" \xC2\xB7 "));
}

} // namespace Jel
