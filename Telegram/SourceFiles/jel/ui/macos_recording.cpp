#include "jel/ui/macos_recording.h"

#include "ui/style/style_core_scale.h"
#include "ui/image/image_prepare.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <algorithm>
#include <array>
#include <cmath>

namespace Jel::Ui {
namespace {

struct Vertex {
	QPointF point;
	QPointF in;
	QPointF out;
};

const auto VoiceHolder = std::array{
	Vertex{ { 0, -9 }, { 2.884, 0 }, { -2.884, 0 } },
	Vertex{ { -5.438, -8.464 }, { 1.112, -0.536 }, { -1.104, 0.532 } },
	Vertex{ { -7.964, -5.938 }, { 0.532, -1.104 }, { -0.536, 1.112 } },
	Vertex{ { -8.5, -0.5 }, { 0, -2.884 }, { 0, 1.993 } },
	Vertex{ { -7.205, 4.052 }, { -0.63, -1.057 }, { 0.63, 1.057 } },
	Vertex{ { -4.586, 6.672 }, { -0.829, -0.535 }, { 0.829, 0.535 } },
	Vertex{ { 0, 8 }, { -2.458, 0 }, { 2.458, 0 } },
	Vertex{ { 4.614, 6.672 }, { -0.793, 0.482 }, { 0.793, -0.482 } },
	Vertex{ { 7.19, 4.052 }, { -0.351, 0.627 }, { 0.351, -0.627 } },
	Vertex{ { 8.5, -0.5 }, { 0, 2.43 }, { 0, -2.884 } },
	Vertex{ { 7.964, -5.938 }, { 0.536, 1.112 }, { -0.532, -1.104 } },
	Vertex{ { 5.438, -8.464 }, { 1.104, 0.532 }, { -1.112, -0.536 } },
};
const auto VideoHolder = std::array{
	Vertex{ { 0, -11 }, { 3.732, 0 }, { -3.732, 0 } },
	Vertex{ { -7.037, -10.307 }, { 1.439, -0.693 }, { -1.429, 0.688 } },
	Vertex{ { -10.307, -7.037 }, { 0.688, -1.429 }, { -0.693, 1.439 } },
	Vertex{ { -11, 0 }, { 0, -3.732 }, { 0, 3.732 } },
	Vertex{ { -10.307, 7.037 }, { -0.693, -1.439 }, { 0.688, 1.429 } },
	Vertex{ { -7.037, 10.307 }, { -1.429, -0.688 }, { 1.439, 0.693 } },
	Vertex{ { 0, 11 }, { -3.732, 0 }, { 3.732, 0 } },
	Vertex{ { 7.037, 10.307 }, { -1.439, 0.693 }, { 1.429, -0.688 } },
	Vertex{ { 10.307, 7.037 }, { -0.688, 1.429 }, { 0.693, -1.439 } },
	Vertex{ { 11, 0 }, { 0, 3.732 }, { 0, -3.732 } },
	Vertex{ { 10.307, -7.037 }, { 0.693, 1.439 }, { -0.688, -1.429 } },
	Vertex{ { 7.037, -10.307 }, { 1.429, 0.688 }, { -1.439, -0.693 } },
};

QPainterPath HolderPath(const std::array<Vertex, 12> &vertices) {
	auto path = QPainterPath();
	const auto center = QPointF(15., 15.);
	path.moveTo(center + vertices.front().point);
	for (auto i = 0; i != int(vertices.size()); ++i) {
		const auto &from = vertices[i];
		const auto &to = vertices[(i + 1) % vertices.size()];
		path.cubicTo(center + from.point + from.out, center + to.point + to.in, center + to.point);
	}
	path.closeSubpath();
	return path;
}

QPainterPath VoicePath() {
	auto path = HolderPath(VoiceHolder);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
	path = path.trimmed(0.2533, 0.7463);
#else
	const auto holder = path;
	path = QPainterPath();
	const auto length = holder.length();
	for (auto i = 0; i <= 256; ++i) {
		const auto fraction = 0.2533 + (0.7463 - 0.2533) * i / 256.;
		const auto point = holder.pointAtPercent(holder.percentAtLength(fraction * length));
		if (i) path.lineTo(point); else path.moveTo(point);
	}
#endif
	path.addRoundedRect(QRectF(10.5, 4., 9., 15.), 4.5, 4.5);
	path.moveTo(15., 27.);
	path.lineTo(15., 23.);
	return path;
}

QPainterPath VideoPath() {
	auto path = HolderPath(VideoHolder);
	path.addEllipse(QRectF(10., 10., 10., 10.));
	return path;
}

void PaintPose(QPainter &p, const QRect &rect, const QPainterPath &path,
		double opacity, const QColor &color) {
	if (opacity <= 0.) return;
	const auto size = style::ConvertScale(30);
	const auto scale = size / 30.;
	const auto poseScale = 0.5 + 0.5 * opacity;
	const auto blur = 5. * (1. - opacity);
	const auto paint = [&](QPainter &p) {
		p.setRenderHint(QPainter::Antialiasing);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.translate(15., 15.);
		p.scale(poseScale, poseScale);
		p.translate(-15., -15.);
		p.drawPath(path);
	};
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.translate(rect.x() + rect.width() / 2., rect.y() + rect.height() / 2.);
	if (blur < 0.05) {
		p.scale(scale, scale);
		p.translate(-15., -15.);
		paint(p);
	} else {
		const auto ratio = style::DevicePixelRatio();
		const auto canvas = style::ConvertScale(50);
		auto image = QImage(canvas * ratio, canvas * ratio, QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			q.scale(scale, scale);
			q.translate(10., 10.);
			paint(q);
		}
		image = Images::BlurLargeImage(std::move(image), std::max(1, int(std::round(blur * scale * ratio))));
		image.setDevicePixelRatio(ratio);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(QRectF(-canvas / 2., -canvas / 2., canvas, canvas), image);
	}
	p.restore();
}

} // namespace

double MacosRecordingSpring(double delta, double progress) {
	constexpr auto rate = 47.118 / 2.;
	const auto damped = std::sqrt(555.027 - rate * rate);
	const auto response = [&](double t) {
		return 1. - std::exp(-rate * t)
			* (std::cos(damped * t) + rate / damped * std::sin(damped * t));
	};
	return delta * response(progress * 0.5) / response(0.5);
}

void PaintMacosRecordingGlyph(QPainter &p, const QRect &rect, double video, const QColor &color) {
	static const auto voice = VoicePath();
	static const auto lens = VideoPath();
	const auto progress = std::clamp(video, 0., 1.);
	PaintPose(p, rect, voice, 1. - progress, color);
	PaintPose(p, rect, lens, progress, color);
}

} // namespace Jel::Ui
