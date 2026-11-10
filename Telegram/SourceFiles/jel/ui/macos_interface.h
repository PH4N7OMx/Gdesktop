#pragma once

#include <memory>

class QString;
class QPainter;
class QWidget;
class QRect;

namespace style {
struct PeerList;
struct MediaSlider;
}

namespace style::internal {
class Icon;
}

namespace Ui {
class RpWidget;
class SettingsButton;
class VerticalLayout;
}

namespace Window {
class SessionController;
}

namespace Jel::Ui {

QString MacosFontFamily(const QString &configured);
void StartMacosInterface();
[[nodiscard]] bool MacosContinuousSliderEnabled(const style::MediaSlider &style);
[[nodiscard]] std::unique_ptr<style::PeerList> PrepareMacosPeerListStyle(const style::PeerList &original);
[[nodiscard]] bool MacosInterfaceEnabled();
[[nodiscard]] bool MacosInterfaceIconsEnabled();
[[nodiscard]] bool MacosDefaultPaletteEnabled();
[[nodiscard]] int MacosTitleControlsInset(not_null<const QWidget*> widget);
::Ui::RpWidget *CreateMacosNavigation(
	not_null<::Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller);
void ApplyMacosPalette();
void SetupMacosSettingsPage(not_null<::Ui::RpWidget*> page);
::Ui::VerticalLayout *CreateMacosSettingsGroup(not_null<::Ui::VerticalLayout*> parent);
void AddMacosSettingsRow(not_null<::Ui::VerticalLayout*> group, not_null<::Ui::SettingsButton*> row);
bool PaintMacosSettingsIcon(
	QPainter &p,
	const QRect &rect,
	const style::internal::Icon *icon);

} // namespace Jel::Ui
