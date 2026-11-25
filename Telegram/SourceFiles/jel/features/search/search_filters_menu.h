#pragma once

#include "base/unique_qptr.h"
#include "jel/features/search/search_filters.h"

#include <QtCore/QPoint>

class PeerData;

namespace Ui {
class PopupMenu;
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Jel {

struct SearchFiltersMenuArgs {
	not_null<Ui::RpWidget*> parent;
	not_null<Window::SessionController*> controller;
	SearchExtraFilters current;
	bool inPeer = false;
	bool isPrivateUser = false;
	bool canChooseFromUser = false;
	bool mediaFilterLocked = false;
	PeerData *fromPeer = nullptr;
	Fn<void(SearchExtraFilters)> onChanged;
	Fn<void(PeerData*)> onSetFromPeer;
};

void ShowSearchDateRangeBox(
	not_null<Window::SessionController*> controller,
	SearchExtraFilters current,
	Fn<void(SearchExtraFilters)> onChanged);

void ShowSearchFiltersMenu(
	base::unique_qptr<Ui::PopupMenu> &menu,
	QPoint globalPos,
	SearchFiltersMenuArgs args);

} // namespace Jel
