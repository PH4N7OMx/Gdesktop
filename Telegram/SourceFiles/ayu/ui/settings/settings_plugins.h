#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Settings {

class AyuPlugins final : public Section<AyuPlugins> {
public:
	AyuPlugins(QWidget *parent, not_null<Window::SessionController*> controller);
	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void refresh();
	Ui::VerticalLayout *_content = nullptr;
	bool _refreshPending = false;

};

} // namespace Settings
