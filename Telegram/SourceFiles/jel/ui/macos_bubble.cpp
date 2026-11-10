#include "jel/ui/macos_bubble.h"

#include "jel/jel_settings.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/chat/message_bubble.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <algorithm>
#include <cmath>

namespace Jel::Ui {
namespace {

// Port of addContinuousCorner / drawMessageBubbleForm from
// TelegramTheme/ChatMessageBubbleImages.swift, pinned in sources.json.
void AddContinuousCorner(QPainterPath &path, QPointF corner, QPointF u,
		QPointF v, double radius, double smoothing) {
	const auto point = [&](double x, double y) { return corner + u * x + v * y; };
	if (radius <= 0.) { path.lineTo(corner); return; }
	constexpr auto pi = 3.14159265358979323846;
	const auto extent = (1. + smoothing) * radius;
	const auto measure = (1. - smoothing) * pi / 2.;
	const auto length = std::sin(measure / 2.) * radius * std::sqrt(2.);
	const auto alpha = (pi / 2. - measure) / 2.;
	const auto beta = pi / 4. * smoothing;
	const auto c = radius * std::tan(alpha / 2.) * std::cos(beta);
	const auto d = c * std::tan(beta);
	const auto b = (extent - length - c - d) / 3.;
	const auto a = 2. * b;
	const auto start = QPointF(extent - a - b - c, d);
	const auto end = start + QPointF(-length, length);
	path.lineTo(point(extent, 0.));
	path.cubicTo(point(extent - a, 0.), point(extent - a - b, 0.), point(start.x(), start.y()));
	const auto handle = 4. / 3. * std::tan(measure / 4.) * radius;
	const auto first = start + QPointF(-std::cos(beta), std::sin(beta)) * handle;
	const auto second = end - QPointF(-std::sin(beta), std::cos(beta)) * handle;
	path.cubicTo(point(first.x(), first.y()), point(second.x(), second.y()), point(end.x(), end.y()));
	path.cubicTo(point(end.x() - d, end.y() + c), point(0., end.y() + b + c), point(0., extent));
}

QPainterPath BubblePath(const ::Ui::SimpleBubble &args) {
	using Corner = ::Ui::BubbleCornerRounding;
	const auto rect = args.geometry;
	const auto w = double(rect.width());
	const auto h = double(rect.height());
	const auto leftTail = args.rounding.bottomLeft == Corner::Tail;
	const auto rightTail = args.rounding.bottomRight == Corner::Tail;
	const auto tail = (leftTail || rightTail) && !JelSettings::getInstance().removeMessageTail();
	// Build in outgoing coordinates, then mirror incoming bubbles.
	const auto incoming = leftTail || (!args.outbg && !rightTail);
	const auto radius = [&](Corner corner) {
		return double(corner == Corner::Tail ? ::Ui::BubbleRadiusLarge() : ::Ui::BubbleCornerRadius(corner));
	};
	auto tl = radius(incoming ? args.rounding.topRight : args.rounding.topLeft);
	auto tr = radius(incoming ? args.rounding.topLeft : args.rounding.topRight);
	auto bl = radius(incoming ? args.rounding.bottomRight : args.rounding.bottomLeft);
	auto br = radius(incoming ? args.rounding.bottomLeft : args.rounding.bottomRight);
	const auto ratio = std::min({1., w / std::max(1., tl + tr), w / std::max(1., bl + br),
		h / std::max(1., tl + bl), h / std::max(1., tr + br)});
	tl *= ratio; tr *= ratio; bl *= ratio; br *= ratio;
	const auto allowed = [](double side, double sum, double fixed = 0.) {
		return sum > 0. ? (side - fixed) / sum - 1. : 0.6;
	};
	const auto smoothing = std::clamp(std::min({0.6, allowed(w, tl + tr),
		allowed(w, bl + (tail ? 0. : br), tail ? br : 0.),
		allowed(h, tl + bl), allowed(h, tr + (tail ? 0. : br), tail ? br : 0.)}), 0., 0.6);
	auto path = QPainterPath();
	path.moveTo(w - (1. + smoothing) * tr, 0.);
	AddContinuousCorner(path, {w, 0.}, {-1., 0.}, {0., 1.}, tr, smoothing);
	AddContinuousCorner(path, {w, h}, {0., -1.}, {-1., 0.}, br, tail ? 0. : smoothing);
	AddContinuousCorner(path, {0., h}, {1., 0.}, {0., -1.}, bl, smoothing);
	AddContinuousCorner(path, {0., 0.}, {0., 1.}, {1., 0.}, tl, smoothing);
	path.closeSubpath();
	if (tail && br > 0.) {
		const auto scale = br / 16.;
		auto lower = QPainterPath();
		lower.moveTo(w - 9. * scale, h - 8.5 * scale);
		lower.quadTo(w - 9. * scale, h, w + 4.5 * scale, h);
		lower.quadTo(w + 18. * scale, h, w + 18. * scale, h - 8.5 * scale);
		lower.closeSubpath();
		lower.addRect(QRectF(w - br, h - br, br, br - 8.5 * scale));
		auto cutout = QPainterPath();
		cutout.addEllipse(QRectF(w, h - 19. * scale, 23. * scale, 21. * scale));
		auto bounds = QPainterPath();
		bounds.addRect(QRectF(0., 0., w + 6. * scale, h));
		path = path.united(lower).subtracted(cutout).intersected(bounds);
	}
	auto transform = QTransform();
	transform.translate(rect.x() + (incoming ? w : 0.), rect.y());
	if (incoming) transform.scale(-1., 1.);
	return transform.map(path);
}

} // namespace

void PaintMacosBubble(QPainter &p, const ::Ui::SimpleBubble &args) {
	if (args.geometry.isEmpty()) return;
	const auto path = BubblePath(args);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	const auto patterned = !args.selected && args.outbg && args.pattern
		&& !args.patternViewport.isEmpty() && !args.pattern->pixmap.isNull();
	if (patterned) {
		const auto &pixmap = args.pattern->pixmap;
		const auto size = pixmap.size() / pixmap.devicePixelRatio();
		auto transform = QTransform();
		transform.translate(args.patternViewport.x(), args.patternViewport.y());
		transform.scale(double(args.patternViewport.width()) / size.width(),
			double(args.patternViewport.height()) / size.height());
		auto brush = QBrush(pixmap);
		brush.setTransform(transform);
		p.setOpacity(p.opacity() * args.st->msgOutBg()->c.alphaF());
		p.fillPath(path, brush);
	} else {
		p.fillPath(path, args.st->messageStyle(args.outbg, args.selected).msgBg->c);
	}
	p.restore();
}

} // namespace Jel::Ui
