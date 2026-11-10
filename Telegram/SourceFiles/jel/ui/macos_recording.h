#pragma once

class QPainter;
class QRect;
class QColor;

namespace Jel::Ui {
void PaintMacosRecordingGlyph(QPainter &p, const QRect &rect, double video, const QColor &color);
double MacosRecordingSpring(double delta, double progress);
} // namespace Jel::Ui
