#pragma once

#include "ui/rp_widget.h"

namespace Window { class SessionController; }

namespace Jel::Ui {

class MacosSidebar final : public ::Ui::RpWidget {
public:
	MacosSidebar(QWidget *parent, not_null<Window::SessionController*> controller);
	~MacosSidebar();
	void showTab(int tab);
	int currentTab() const;
	rpl::producer<int> tabChanges() const;
protected:
	void keyPressEvent(QKeyEvent *event) override;
private:
	struct State;
	std::unique_ptr<State> _state;

};

} // namespace Jel::Ui
