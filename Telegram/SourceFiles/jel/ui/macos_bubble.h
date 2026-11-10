#pragma once

namespace Ui { struct SimpleBubble; }
class QPainter;

namespace Jel::Ui {
void PaintMacosBubble(QPainter &p, const ::Ui::SimpleBubble &args);
} // namespace Jel::Ui
