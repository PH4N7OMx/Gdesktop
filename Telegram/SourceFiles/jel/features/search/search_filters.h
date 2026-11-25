#pragma once

#include "jel/features/search/search_filters_core.h"
#include "scheme.h"

#include <QtCore/QDate>
#include <QtCore/QString>

namespace Jel {

[[nodiscard]] std::int32_t PackSearchDate(QDate date);
[[nodiscard]] QDate UnpackSearchDate(std::int32_t packed);

[[nodiscard]] SearchDateBounds ResolveSearchDateBounds(
	const SearchExtraFilters &filters,
	QDate today = QDate::currentDate());

[[nodiscard]] MTPMessagesFilter SearchMediaFilterToMTP(
	SearchMediaFilter filter);

[[nodiscard]] QString SearchExtraFiltersToken(
	const SearchExtraFilters &filters);

[[nodiscard]] QString SearchMediaFilterLabel(SearchMediaFilter filter);
[[nodiscard]] QString SearchDateFilterLabel(const SearchExtraFilters &filters);
[[nodiscard]] QString SearchSenderFilterLabel(SearchSenderFilter filter);
[[nodiscard]] QString SearchExtraFiltersSummary(
	const SearchExtraFilters &filters);

} // namespace Jel
