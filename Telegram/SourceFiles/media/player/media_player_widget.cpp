/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_widget.h"

#include "platform/platform_specific.h"
#include "jel/ui/macos_interface.h"
#include "base/event_filter.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/format_values.h"
#include "ui/text/format_song_document_name.h"
#include "lang/lang_keys.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_playback_progress.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_dropdown.h"
#include "media/player/media_player_volume_controller.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "storage/storage_account.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "styles/style_media_player.h"
#include "styles/style_jel_styles.h"

#include <QtCore/QEasingCurve>

namespace Media {
namespace Player {
namespace {

[[nodiscard]] QColor PlayerCapsuleColor() {
	const auto bg = st::windowBg->c;
	const auto fg = st::windowFg->c;
	return QColor(
		bg.red() + (fg.red() - bg.red()) * 0.04,
		bg.green() + (fg.green() - bg.green()) * 0.04,
		bg.blue() + (fg.blue() - bg.blue()) * 0.04);
}

[[nodiscard]] const style::MediaPlayerPlayButton &PlayerPlayStyle() {
	if (!Jel::Ui::MacosInterfaceEnabled()) {
		return st::mediaPlayerPlayButton;
	}
	static const auto result = [] {
		auto copy = st::mediaPlayerPlayButton;
		copy.size = style::ConvertScale(QSize(34, 34));
		copy.iconPosition = QPoint((copy.size.width() - copy.icon.size.width()) / 2,
			(copy.size.height() - copy.icon.size.height()) / 2);
		copy.rippleAreaSize = style::ConvertScale(34);
		copy.rippleAreaPosition = QPoint();
		return copy;
	}();
	return result;
}

[[nodiscard]] const style::IconButton &PlayerSkipStyle(bool previous) {
	const auto &original = previous ? st::mediaPlayerPreviousButton : st::mediaPlayerNextButton;
	if (!Jel::Ui::MacosInterfaceEnabled()) {
		return original;
	}
	const auto prepare = [](const style::IconButton &original) {
		auto copy = original;
		copy.width = copy.height = style::ConvertScale(30);
		copy.iconPosition = QPoint(-1, -1);
		copy.rippleAreaPosition = QPoint();
		copy.rippleAreaSize = style::ConvertScale(30);
		return copy;
	};
	static const auto previousStyle = prepare(st::mediaPlayerPreviousButton);
	static const auto nextStyle = prepare(st::mediaPlayerNextButton);
	return previous ? previousStyle : nextStyle;
}

[[nodiscard]] QString SeekTime(crl::time milliseconds) {
	const auto seconds = std::max(crl::time(0), milliseconds) / 1000;
	const auto minutes = seconds / 60;
	return (minutes >= 60
		? QString::number(minutes / 60) + ':'
			+ QString::number(minutes % 60).rightJustified(2, '0')
		: QString::number(minutes))
		+ ':' + QString::number(seconds % 60).rightJustified(2, '0');
}

class SeekTooltip final : public Ui::RpWidget {
public:
	explicit SeekTooltip(QWidget *parent) : RpWidget(parent) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		hide();
	}

	void setTimes(crl::time position, crl::time duration) {
		const auto current = SeekTime(position);
		const auto total = u"  "_q + SeekTime(duration);
		if (_current == current && _total == total) {
			return;
		}
		_current = current;
		_total = total;
		const auto font = st::macosPlayerName.style.font;
		_currentWidth = font->width(_current);
		resize(_currentWidth + font->width(_total) + style::ConvertScale(16), style::ConvertScale(24));
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		const auto font = st::macosPlayerName.style.font;
		const auto baseline = (height() - font->height) / 2 + font->ascent;
		const auto left = style::ConvertScale(8);
		p.setFont(font);
		p.setPen(st::windowFg);
		p.drawText(left, baseline, _current);
		p.setPen(st::windowSubTextFg);
		p.drawText(left + _currentWidth, baseline, _total);
	}

private:
	QString _current;
	QString _total;
	int _currentWidth = 0;

};

class PlaybackSlider final : public Ui::FilledSlider {
public:
	PlaybackSlider(QWidget *parent, const style::FilledSlider &st, QWidget *tooltipParent)
	: FilledSlider(parent, st)
	, _tooltip(std::in_place, tooltipParent) {
		if (Jel::Ui::MacosInterfaceEnabled()) {
			setMouseTracking(true);
			base::install_event_filter(this, window(), [=](not_null<QEvent*> event) {
				if (event->type() == QEvent::WindowDeactivate) {
					clearPreview();
				}
				return base::EventFilterResult::Continue;
			});
		}
	}

	void setBufferedRanges(std::vector<std::pair<float64, float64>> ranges) {
		_bufferedRanges = std::move(ranges);
		update();
	}

	void setExpansionProgress(float64 progress) {
		_expansionProgress = progress;
		updateTooltip();
		update();
	}

	void setDuration(crl::time duration) {
		const auto value = std::max(crl::time(0), duration);
		if (_duration != value) {
			_duration = value;
			update();
		}
		updateTooltip();
	}

protected:
	void mouseMoveEvent(QMouseEvent *e) override {
		updatePreview(e->pos());
		FilledSlider::mouseMoveEvent(e);
	}

	void mousePressEvent(QMouseEvent *e) override {
		updatePreview(e->pos());
		FilledSlider::mousePressEvent(e);
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (rect().contains(e->pos())) {
			updatePreview(e->pos());
		} else {
			clearPreview();
		}
		FilledSlider::mouseReleaseEvent(e);
	}

	void enterEventHook(QEnterEvent *e) override {
		FilledSlider::enterEventHook(e);
		updatePreview(mapFromGlobal(QCursor::pos()));
	}

	void leaveEventHook(QEvent *e) override {
		FilledSlider::leaveEventHook(e);
		if (!isChanging()) {
			clearPreview();
		}
	}

	void hideEvent(QHideEvent *e) override {
		clearPreview();
		FilledSlider::hideEvent(e);
	}

	void paintEvent(QPaintEvent *e) override {
		if (!Jel::Ui::MacosInterfaceEnabled()) {
			FilledSlider::paintEvent(e);
			return;
		}
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setOpacity(fadeOpacity());
		const auto line = style::ConvertScale(2.)
			+ style::ConvertScale(4.) * _expansionProgress;
		const auto track = QRectF(0., (height() - line) / 2., width(), line);
		const auto fg = st::windowFg->c;
		auto color = fg;
		color.setAlphaF(0.2 * _expansionProgress);
		p.setBrush(color);
		p.drawRoundedRect(track, line / 2., line / 2.);
		const auto draw = [&](float64 value, float64 alpha) {
			p.save();
			p.setClipRect(QRectF(0., 0., width() * std::clamp(value, 0., 1.), height()));
			color = fg;
			color.setAlphaF(alpha);
			p.setBrush(color);
			p.drawRoundedRect(track, line / 2., line / 2.);
			p.restore();
		};
		for (const auto &[from, till] : _bufferedRanges) {
			if (getCurrentValue() < from || getCurrentValue() >= till) continue;
			p.save();
			p.setClipRect(QRectF(width() * from, 0., width() * (till - from), height()));
			color = fg;
			color.setAlphaF(0.3);
			p.setBrush(color);
			p.drawRoundedRect(track, line / 2., line / 2.);
			p.restore();
		}
		if (_hoverProgress && _duration > 0 && _expansionProgress > 0. && !isDisabled()) {
			draw(*_hoverProgress, 0.25);
		}
		draw(getCurrentValue(), isDisabled() ? 0.3 : 1.);
	}

private:
	void updatePreview(QPoint position) {
		if (!Jel::Ui::MacosInterfaceEnabled() || isDisabled() || !width()) {
			clearPreview();
			return;
		}
		_hoverProgress = std::clamp(position.x() / float64(width()), 0., 1.);
		updateTooltip();
		update();
	}

	void clearPreview() {
		_hoverProgress.reset();
		_tooltip->hide();
		update();
	}

	void updateTooltip() {
		if (!_hoverProgress || _expansionProgress <= 0. || !_duration
			|| isHidden() || isDisabled() || !window()->isActiveWindow()) {
			_tooltip->hide();
			return;
		}
		_tooltip->setTimes(crl::time(*_hoverProgress * _duration), _duration);
		const auto line = style::ConvertScale(2.) + style::ConvertScale(4.) * _expansionProgress;
		const auto x = std::clamp(int(base::SafeRound(width() * *_hoverProgress))
			+ style::ConvertScale(12), 0, std::max(0, width() - _tooltip->width()));
		const auto y = int(std::ceil((height() + line) / 2.)) + style::ConvertScale(4);
		_tooltip->move(_tooltip->parentWidget()->mapFromGlobal(mapToGlobal(QPoint(x, y))));
		_tooltip->show();
		_tooltip->raise();
	}

	std::vector<std::pair<float64, float64>> _bufferedRanges;
	float64 _expansionProgress = 0.;
	crl::time _duration = 0;
	std::optional<float64> _hoverProgress;
	base::unique_qptr<SeekTooltip> _tooltip;

};

} // namespace

Widget::Widget(
	QWidget *parent,
	not_null<Ui::RpWidget*> dropdownsParent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _orderMenuParent(dropdownsParent)
, _nameLabel(this, Jel::Ui::MacosInterfaceEnabled() ? st::macosPlayerName : st::mediaPlayerName)
, _artistLabel(this, st::macosPlayerArtist)
, _rightControls(this, object_ptr<Ui::RpWidget>(this))
, _timeLabel(rightControls(), st::mediaPlayerTime)
, _playPause(this, PlayerPlayStyle())
, _volumeToggle(rightControls(), st::mediaPlayerVolumeToggle)
, _repeatToggle(rightControls(), st::mediaPlayerRepeatButton)
, _orderToggle(rightControls(), st::mediaPlayerOrderButton)
, _speedToggle(rightControls(), st::mediaPlayerSpeedButton)
, _close(this, st::mediaPlayerClose)
, _shadow(this)
, _playbackSlider(object_ptr<PlaybackSlider>(this, st::mediaPlayerPlayback, dropdownsParent))
, _volume(std::in_place, dropdownsParent.get())
, _playbackProgress(std::make_unique<View::PlaybackProgress>())
, _orderController(
	std::make_unique<OrderController>(
		_orderToggle.data(),
		dropdownsParent,
		[=](bool over) { markOver(over); },
		Core::App().settings().playerOrderModeValue(),
		[=](OrderMode value) { saveOrder(value); }))
, _speedController(
	std::make_unique<SpeedController>(
		_speedToggle.data(),
		_speedToggle->st(),
		dropdownsParent,
		[=](bool over) { markOver(over); },
		[=](bool lastNonDefault) { return speedLookup(lastNonDefault); },
		[=](float64 speed) { saveSpeed(speed); })) {
	_speedController->setApplySpeed([=](float64 speed) {
		instance()->setPlaybackSpeed(speed);
	});
	_speedController->realtimeValue(
	) | rpl::on_next([=](float64 speed) {
		_speedToggle->setSpeed(speed);
		_speedToggle->setAccessibleName(tr::lng_mediaview_playback_speed(
			tr::now,
			lt_speed,
			QString::number(base::SafeRound(speed * 10) / 10.) + "x"));
	}, _speedToggle->lifetime());
	_speedToggle->finishAnimating();

	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
	resize(width(), Jel::Ui::MacosInterfaceEnabled()
		? style::ConvertScale(52)
		: st::mediaPlayerHeight + st::lineWidth);
	if (Jel::Ui::MacosInterfaceEnabled()) {
		_shadow->hide();
		base::install_event_filter(this, window(), [=](not_null<QEvent*> event) {
			if (event->type() == QEvent::WindowDeactivate
				|| event->type() == QEvent::WindowActivate) {
				InvokeQueued(this, [=] { updateExpansion(); });
			}
			return base::EventFilterResult::Continue;
		});
	}

	if (Jel::Ui::MacosInterfaceIconsEnabled()) {
		_playPause->enableMacosIcons();
	}
	setupRightControls();

	_volumeToggle->setAccessibleName(tr::lng_ringtones_box_volume(tr::now));
	_repeatToggle->setAccessibleName(tr::lng_schedule_repeat_label(tr::now));
	_orderToggle->setAccessibleName(tr::lng_sr_playback_order(tr::now));
	_close->setAccessibleName(tr::lng_sr_player_close(tr::now));

	_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	_artistLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	_artistLabel->hide();
	_timeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (Jel::Ui::MacosInterfaceEnabled()) {
		_timeLabel->hide();
	}

	_playbackProgress->setInLoadingStateChangedCallback([=](bool loading) {
		_playbackSlider->setDisabled(loading);
		static_cast<PlaybackSlider*>(_playbackSlider.data())->setDuration(_lastDurationMs);
	});
	_playbackProgress->setValueChangedCallback([=](float64 value, float64) {
		_playbackSlider->setValue(value);
	});
	_playbackSlider->setChangeProgressCallback([=](float64 value) {
		if (_type != AudioMsgId::Type::Song && _type != AudioMsgId::Type::Voice) {
			return;
		}
		_playbackProgress->setValue(value, false);
		handleSeekProgress(value);
	});
	_playbackSlider->setChangeFinishedCallback([=](float64 value) {
		if (_type != AudioMsgId::Type::Song && _type != AudioMsgId::Type::Voice) {
			return;
		}
		_playbackProgress->setValue(value, false);
		handleSeekFinished(value);
	});
	_playPause->setClickedCallback([=] {
		instance()->playPauseCancelClicked(_type);
	});

	updateVolumeToggleIcon();
	_volumeToggle->setClickedCallback([=] {
		const auto volume = (Core::App().settings().songVolume() > 0)
			? 0.
			: Core::App().settings().rememberedSongVolume();
		Core::App().settings().setSongVolume(volume);
		Core::App().saveSettingsDelayed();
		mixer()->setSongVolume(volume);
	});
	Core::App().settings().songVolumeChanges(
	) | rpl::on_next([=] {
		updateVolumeToggleIcon();
	}, lifetime());

	Core::App().settings().playerRepeatModeValue(
	) | rpl::on_next([=] {
		updateRepeatToggleIcon();
	}, lifetime());

	_repeatToggle->setClickedCallback([=] {
		if (Jel::Ui::MacosInterfaceEnabled() && _type == AudioMsgId::Type::Song) {
			_playlistButtonRequests.fire(QRect(
				_repeatToggle->mapToGlobal(QPoint()), _repeatToggle->size()));
			return;
		}
		auto &settings = Core::App().settings();
		settings.setPlayerRepeatMode([&] {
			switch (settings.playerRepeatMode()) {
			case RepeatMode::None: return RepeatMode::One;
			case RepeatMode::One: return RepeatMode::All;
			case RepeatMode::All: return RepeatMode::None;
			}
			Unexpected("Repeat mode in Settings.");
		}());
		Core::App().saveSettingsDelayed();
	});

	_speedController->saved(
	) | rpl::on_next([=] {
		instance()->updatePlaybackSpeed();
	}, lifetime());

	rpl::merge(
		Core::App().settings().voicePlaybackSpeedChanges() | rpl::to_empty,
		Core::App().settings().audioPlaybackSpeedChanges() | rpl::to_empty
	) | rpl::on_next([=] {
		_speedController->reloadFromLookup();
	}, lifetime());

	instance()->trackChanged(
	) | rpl::filter([=](AudioMsgId::Type type) {
		return (type == _type);
	}) | rpl::on_next([=](AudioMsgId::Type type) {
		handleSongChange();
		updateControlsVisibility();
		updateLabelsGeometry();
	}, lifetime());

	rpl::merge(
		instance()->tracksFinished(
		) | rpl::filter([=](AudioMsgId::Type type) {
			return (type == AudioMsgId::Type::Voice);
		}) | rpl::to_empty,
		instance()->stops(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] {
		_voiceIsActive = false;
		const auto currentSong = instance()->current(AudioMsgId::Type::Song);
		const auto songState = instance()->getState(AudioMsgId::Type::Song);
		if (currentSong == songState.id && !IsStoppedOrStopping(songState.state)) {
			setType(AudioMsgId::Type::Song);
		}
	}, lifetime());

	instance()->updatedNotifier(
	) | rpl::on_next([=](const TrackState &state) {
		handleSongUpdate(state);
	}, lifetime());

	PrepareVolumeDropdown(_volume.get(), controller, _volumeToggle->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Wheel);
	}) | rpl::map([=](not_null<QEvent*> e) {
		return not_null{ static_cast<QWheelEvent*>(e.get()) };
	}));
	_volumeToggle->installEventFilter(_volume.get());
	_volume->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Enter) {
			markOver(true);
		} else if (e->type() == QEvent::Leave) {
			markOver(false);
		}
	}, _volume->lifetime());

	hidePlaylistOn(_playPause);
	hidePlaylistOn(_close);
	hidePlaylistOn(_rightControls);

	hideDropdownsOn(_volumeToggle);
	hideDropdownsOn(_orderToggle);
	hideDropdownsOn(_speedToggle);

	const auto otherDropdownCheck = [=](QPoint globalPosition) {
		return overDropdownControl(globalPosition);
	};
	_orderController->setOtherDropdownCheck(otherDropdownCheck);
	_speedController->setOtherDropdownCheck(otherDropdownCheck);

	setType(AudioMsgId::Type::Song);
	_playPause->finishTransform();
}

void Widget::hidePlaylistOn(not_null<Ui::RpWidget*> widget) {
	widget->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::on_next([=] {
		updateOverLabelsState(false);
	}, widget->lifetime());
}

void Widget::hideDropdownsOn(not_null<Ui::RpWidget*> widget) {
	widget->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::on_next([=] {
		hideDropdowns(widget);
	}, widget->lifetime());
}

bool Widget::overDropdownControl(QPoint globalPosition) const {
	const auto over = [&](not_null<Ui::RpWidget*> control) {
		return !control->isHidden()
			&& control->rect().contains(
				control->mapFromGlobal(globalPosition));
	};
	return over(_volumeToggle)
		|| over(_orderToggle)
		|| over(_speedToggle);
}

void Widget::hideDropdowns(not_null<Ui::RpWidget*> except) {
	if (except.get() != _volumeToggle.data()) {
		_volume->hideFast();
	}
	if (except.get() != _orderToggle.data()) {
		if (const auto menu = _orderController->menu()) {
			menu->hideFast();
		}
	}
	if (except.get() != _speedToggle.data()) {
		if (const auto menu = _speedController->menu()) {
			menu->hideFast();
		}
	}
}

void Widget::setupRightControls() {
	const auto raw = rightControls();
	raw->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(raw);
		if (Jel::Ui::MacosInterfaceEnabled()) {
			p.fillRect(clip, PlayerCapsuleColor());
			return;
		}
		const auto &icon = st::mediaPlayerControlsFade;
		const auto fade = QRect(0, 0, icon.width(), raw->height());
		if (fade.intersects(clip)) {
			icon.fill(p, fade);
		}
		const auto fill = clip.intersected(
			{ icon.width(), 0, raw->width() - icon.width(), raw->height() });
		if (!fill.isEmpty()) {
			p.fillRect(fill, st::mediaPlayerBg);
		}
	}, raw->lifetime());
	_rightControls->show(anim::type::instant);
}

void Widget::updateVolumeToggleIcon() {
	_volumeToggle->setIconOverride([] {
		const auto volume = Core::App().settings().songVolume();
		return (volume == 0.)
			? &st::mediaPlayerVolumeIcon0
			: (volume < 0.66)
			? &st::mediaPlayerVolumeIcon1
			: nullptr;
	}());
}

void Widget::setCloseCallback(Fn<void()> callback) {
	_closeCallback = std::move(callback);
	_close->setClickedCallback([this] { stopAndClose(); });
}

void Widget::setShowItemCallback(
		Fn<void(not_null<const HistoryItem*>)> callback) {
	_showItemCallback = std::move(callback);
}

void Widget::stopAndClose() {
	_voiceIsActive = false;
	if (_type == AudioMsgId::Type::Voice) {
		const auto songData = instance()->current(AudioMsgId::Type::Song);
		const auto songState = instance()->getState(AudioMsgId::Type::Song);
		if (songData == songState.id && !IsStoppedOrStopping(songState.state)) {
			instance()->stop(AudioMsgId::Type::Voice);
			return;
		}
	}
	if (_closeCallback) {
		_closeCallback();
	}
}

void Widget::setShadowGeometryToLeft(int x, int y, int w, int h) {
	_shadow->setGeometryToLeft(x, y, w, h);
}

void Widget::showShadowAndDropdowns() {
	_shadowSuppressed = false;
	_shadow->setVisible(!Jel::Ui::MacosInterfaceEnabled());
	_playbackSlider->setVisible(_type == AudioMsgId::Type::Song || _type == AudioMsgId::Type::Voice);
	if (_volumeHidden) {
		_volumeHidden = false;
		_volume->show();
	}
	_speedController->showBack();
	_orderController->showBack();
}

void Widget::updateDropdownsGeometry() {
	const auto dropdownWidth = st::mediaPlayerVolumeSize.width();
	const auto position = _volume->parentWidget()->mapFromGlobal(
		_volumeToggle->mapToGlobal(
			QPoint(
				(_volumeToggle->width() - dropdownWidth) / 2,
				Jel::Ui::MacosInterfaceEnabled()
					? height() - _volumeToggle->mapTo(this, QPoint()).y()
					: height())));
	const auto playerMargins = _volume->getMargin();
	const auto shift = QPoint(playerMargins.left(), playerMargins.top());
	_volume->move(position - shift);

	_orderController->updateDropdownGeometry();
	_speedController->updateDropdownGeometry();
}

void Widget::hideShadowAndDropdowns() {
	_shadowSuppressed = true;
	_shadow->hide();
	_playbackSlider->hide();
	if (!_volume->isHidden()) {
		_volumeHidden = true;
		_volume->hide();
	}
	_speedController->hideTemporarily();
	_orderController->hideTemporarily();
}

void Widget::raiseDropdowns() {
	_volume->raise();
}

Widget::~Widget() = default;

not_null<Ui::RpWidget*> Widget::rightControls() {
	return _rightControls->entity();
}

void Widget::handleSeekProgress(float64 progress) {
	if (!_lastDurationMs) return;

	const auto positionMs = std::clamp(
		static_cast<crl::time>(progress * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		updateTimeLabel();

		instance()->startSeeking(_type);
	}
}

void Widget::handleSeekFinished(float64 progress) {
	if (!_lastDurationMs) return;

	_seekPositionMs = -1;

	instance()->finishSeeking(_type, progress);
}

void Widget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
	_narrow = (width() < st::mediaPlayerWideWidth);
	updateControlsWrapVisibility();
}

void Widget::updateControlsGeometry() {
	const auto native = Jel::Ui::MacosInterfaceEnabled();
	const auto top = native
		? style::ConvertScale(12) + (style::ConvertScale(40) - _close->height()) / 2
		: st::mediaPlayerPlayTop;
	_close->moveToRight(native ? style::ConvertScale(24) : st::mediaPlayerCloseRight, top);
	auto right = 0;
	if (hasPlaybackSpeedControl()) {
		_speedToggle->moveToRight(right, 0); right += _speedToggle->width();
	}
	if (_type == AudioMsgId::Type::Song) {
		_repeatToggle->moveToRight(right, 0); right += _repeatToggle->width();
		if (!native) {
			_orderToggle->moveToRight(right, 0); right += _orderToggle->width();
		}
	}
	_volumeToggle->moveToRight(right, 0); right += _volumeToggle->width();

	updateControlsWrapGeometry();

	updatePlayPrevNextPositions();

	if (native) {
		const auto line = style::ConvertScale(2.)
			+ style::ConvertScale(4.) * _expansionProgress;
		const auto padding = (line + style::ConvertScale(2.)) * _expansionProgress;
		const auto sliderHeight = int(std::ceil(line + 2 * padding));
		const auto inset = style::ConvertScale(32);
		_playbackSlider->setGeometry(inset, height() - sliderHeight,
			std::max(0, width() - 2 * inset), sliderHeight);
		static_cast<PlaybackSlider*>(_playbackSlider.data())->setExpansionProgress(_expansionProgress);
	} else {
		_playbackSlider->setGeometry(
			0,
			height() - st::mediaPlayerPlayback.fullWidth,
			width(),
			st::mediaPlayerPlayback.fullWidth);
	}

	updateDropdownsGeometry();
}

void Widget::updateControlsWrapGeometry() {
	const auto native = Jel::Ui::MacosInterfaceEnabled();
	const auto controls = getTimeRight()
		+ (native ? 0 : _timeLabel->width() + st::mediaPlayerControlsFade.width());
	rightControls()->resize(controls, _repeatToggle->height());
	_rightControls->move(
		width() - (Jel::Ui::MacosInterfaceEnabled()
			? style::ConvertScale(24) : st::mediaPlayerCloseRight) - _close->width() - controls,
		Jel::Ui::MacosInterfaceEnabled()
			? style::ConvertScale(12) + (style::ConvertScale(40) - rightControls()->height()) / 2
			: st::mediaPlayerPlayTop);
}

void Widget::updateControlsWrapVisibility() {
	_rightControls->toggle(
		Jel::Ui::MacosInterfaceEnabled() || _over || !_narrow,
		isHidden() ? anim::type::instant : anim::type::normal);
}

void Widget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (Jel::Ui::MacosInterfaceEnabled()) {
		p.fillRect(e->rect(), st::windowBg);
		p.setRenderHint(QPainter::Antialiasing);
		const auto inset = style::ConvertScale(12);
		const auto radius = style::ConvertScale(20);
		const auto capsule = QRectF(inset, inset,
			std::max(0, width() - 2 * inset), height() - inset);
		p.setBrush(Qt::NoBrush);
		for (auto step = 8; step > 0; --step) {
			const auto spread = style::ConvertScale(step * 1.5);
			p.setPen(QPen(QColor(0, 0, 0, 4), spread));
			p.drawRoundedRect(capsule, radius, radius);
		}
		const auto dark = st::windowBg->c.lightnessF() < 0.5;
		auto rim = dark ? QColor(Qt::white) : QColor(Qt::black);
		rim.setAlphaF(dark ? 0.10 : 0.04);
		p.setPen(QPen(rim, style::ConvertScale(0.5)));
		p.setBrush(PlayerCapsuleColor());
		p.drawRoundedRect(capsule, radius, radius);
		return;
	}
	auto fill = e->rect().intersected(
		QRect(0, 0, width(), st::mediaPlayerHeight + st::lineWidth));
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::mediaPlayerBg);
	}
}

void Widget::enterEventHook(QEnterEvent *e) {
	markOver(true);
}

void Widget::leaveEventHook(QEvent *e) {
	markOver(false);
}

void Widget::markOver(bool over) {
	if (over) {
		_over = true;
		_wontBeOver = false;
		InvokeQueued(this, [=] {
			updateControlsWrapVisibility();
			updateExpansion();
		});
	} else {
		_wontBeOver = true;
		InvokeQueued(this, [=] {
			if (!_wontBeOver) {
				return;
			}
			_wontBeOver = false;
			_over = false;
			updateControlsWrapVisibility();
			updateExpansion();
		});
		updateOverLabelsState(false);
	}
}

void Widget::updateExpansion() {
	if (!Jel::Ui::MacosInterfaceEnabled()) {
		return;
	}
	const auto expanded = _over && window()->isActiveWindow();
	if (_expanded == expanded) {
		return;
	}
	_expanded = expanded;
	const auto target = expanded ? 1. : 0.;
	const auto distance = std::abs(target - _expansionProgress);
	if (distance < 0.0001) {
		_expansionAnimation.stop();
		return;
	}
	_expansionAnimation.start([=](float64 progress) {
		_expansionProgress = std::clamp(progress, 0., 1.);
		resize(width(), style::ConvertScale(52)
			+ int(base::SafeRound(style::ConvertScale(14) * _expansionProgress)));
		updateControlsGeometry();
		update();
	}, _expansionProgress, target, std::max(crl::time(17), crl::time(150 * distance)),
	[](float64 delta, float64 dt) {
		static const auto curve = [] {
			auto result = QEasingCurve(QEasingCurve::BezierSpline);
			result.addCubicBezierSegment(QPointF(0., 0.), QPointF(0.2, 1.), QPointF(1., 1.));
			return result;
		}();
		return delta * curve.valueForProgress(dt);
	});
}

void Widget::saveOrder(OrderMode mode) {
	Core::App().settings().setPlayerOrderMode(mode);
	Core::App().saveSettingsDelayed();
}

float64 Widget::speedLookup(bool lastNonDefault) const {
	const auto &settings = Core::App().settings();
	return (_type == AudioMsgId::Type::Song)
		? settings.audioPlaybackSpeed(lastNonDefault)
		: settings.voicePlaybackSpeed(lastNonDefault);
}

void Widget::saveSpeed(float64 speed) {
	auto &settings = Core::App().settings();
	if (_type == AudioMsgId::Type::Song) {
		settings.setAudioPlaybackSpeed(speed);
	} else {
		settings.setVoicePlaybackSpeed(speed);
	}
	Core::App().saveSettingsDelayed();
}

void Widget::mouseMoveEvent(QMouseEvent *e) {
	updateOverLabelsState(e->pos());
}

void Widget::mousePressEvent(QMouseEvent *e) {
	_labelsDown = _labelsOver;
}

void Widget::mouseReleaseEvent(QMouseEvent *e) {
	if (auto downLabels = base::take(_labelsDown)) {
		if (_labelsOver != downLabels) {
			return;
		}
		if ((_type == AudioMsgId::Type::Voice)
				|| _lastSongFromAnotherSession) {
			const auto current = instance()->current(_type);
			const auto document = current.audio();
			const auto context = current.contextId();
			if (document && context && _showItemCallback) {
				if (const auto item = document->owner().message(context)) {
					_showItemCallback(item);
				}
			}
		}
	}
}

void Widget::updateOverLabelsState(QPoint pos) {
	const auto left = getNameLeft();
	const auto right = width()
		- _rightControls->x()
		- _rightControls->width()
		+ getTimeRight();
	const auto native = Jel::Ui::MacosInterfaceEnabled();
	const auto labels = myrtlrect(left, native ? style::ConvertScale(12) : 0,
		std::max(0, width() - right - left), native
			? style::ConvertScale(40)
			: height() - st::mediaPlayerPlayback.fullWidth);
	const auto over = labels.contains(pos);
	updateOverLabelsState(over);
}

void Widget::updateOverLabelsState(bool over) {
	_labelsOver = over;
	const auto pressShowsItem = _labelsOver
		&& ((_type == AudioMsgId::Type::Voice)
			|| _lastSongFromAnotherSession);
	setCursor(pressShowsItem ? style::cur_pointer : style::cur_default);
	_togglePlaylistRequests.fire(over && (_type == AudioMsgId::Type::Song)
		&& !Jel::Ui::MacosInterfaceEnabled());
}

void Widget::updatePlayPrevNextPositions() {
	const auto native = Jel::Ui::MacosInterfaceEnabled();
	auto left = native ? style::ConvertScale(22) : st::mediaPlayerPlayLeft;
	const auto position = [&](not_null<QWidget*> button) {
		return native
			? style::ConvertScale(12) + (style::ConvertScale(40) - button->height()) / 2
			: st::mediaPlayerPlayTop;
	};
	const auto skip = native ? style::ConvertScale(5) : st::mediaPlayerPlaySkip;
	const auto compact = native && width() < style::ConvertScale(320);
	if (_previousTrack && native) {
		_previousTrack->setVisible(!compact);
		_nextTrack->setVisible(!compact);
	}
	if (_previousTrack && !compact) {
		_previousTrack->moveToLeft(left, position(_previousTrack.data())); left += _previousTrack->width() + skip;
		_playPause->moveToLeft(left, position(_playPause.data())); left += _playPause->width() + skip;
		_nextTrack->moveToLeft(left, position(_nextTrack.data()));
	} else {
		_playPause->moveToLeft(left, position(_playPause.data()));
	}
	updateLabelsGeometry();
}

int Widget::getNameLeft() const {
	auto result = (Jel::Ui::MacosInterfaceEnabled()
		? style::ConvertScale(22) : st::mediaPlayerPlayLeft) + _playPause->width();
	if (_previousTrack && (!Jel::Ui::MacosInterfaceEnabled() || width() >= style::ConvertScale(320))) {
		const auto skip = Jel::Ui::MacosInterfaceEnabled() ? style::ConvertScale(5) : st::mediaPlayerPlaySkip;
		result += _previousTrack->width() + _nextTrack->width() + 2 * skip;
	}
	result += Jel::Ui::MacosInterfaceEnabled() ? style::ConvertScale(20) : st::mediaPlayerPadding;
	return result;
}

int Widget::getNameRight() const {
	return (Jel::Ui::MacosInterfaceEnabled()
		? style::ConvertScale(24) : st::mediaPlayerCloseRight)
		+ _close->width()
		+ st::mediaPlayerPadding;
}

int Widget::getTimeRight() const {
	auto result = 0;
	result += _volumeToggle->width();
	if (_type == AudioMsgId::Type::Song) {
		result += _repeatToggle->width();
		if (!Jel::Ui::MacosInterfaceEnabled()) {
			result += _orderToggle->width();
		}
	}
	if (hasPlaybackSpeedControl()) {
		result += _speedToggle->width();
	}
	result += st::mediaPlayerPadding;
	return result;
}

void Widget::updateLabelsGeometry() {
	updateControlsWrapGeometry();
	const auto left = getNameLeft();
	const auto native = Jel::Ui::MacosInterfaceEnabled();
	const auto widthForName = width()
		- left
		- getNameRight()
		- (native ? rightControls()->width() : 0);
	const auto showMetadata = !native || widthForName >= style::ConvertScale(40);
	_nameLabel->setVisible(showMetadata);
	_artistLabel->setVisible(native && _hasArtist && showMetadata);
	_nameLabel->resizeToNaturalWidth(std::max(0, widthForName));
	if (native && _hasArtist) {
		_artistLabel->resizeToNaturalWidth(std::max(0, widthForName));
		const auto gap = std::clamp((style::ConvertScale(40)
			- _nameLabel->height() - _artistLabel->height()) / 2, 0, style::ConvertScale(4));
		_nameLabel->moveToLeft(left, style::ConvertScale(12) + gap);
		_artistLabel->moveToLeft(left, style::ConvertScale(52) - gap - _artistLabel->height());
	} else {
		_nameLabel->moveToLeft(left, native
			? style::ConvertScale(12) + (style::ConvertScale(40) - _nameLabel->height()) / 2
			: st::mediaPlayerNameTop - st::mediaPlayerName.style.font->ascent);
	}

	const auto right = getTimeRight();
	_timeLabel->moveToRight(right, Jel::Ui::MacosInterfaceEnabled()
		? (rightControls()->height() - _timeLabel->height()) / 2
		: st::mediaPlayerNameTop - st::mediaPlayerTime.font->ascent);

	updateControlsWrapGeometry();
}

void Widget::updateRepeatToggleIcon() {
	if (Jel::Ui::MacosInterfaceEnabled() && _type == AudioMsgId::Type::Song) {
		const auto nativeIcons = Jel::Ui::MacosInterfaceIconsEnabled();
		_repeatToggle->setIconOverride(
			nativeIcons ? &st::macosPlayerList : &st::macosPlayerListDesktop,
			nativeIcons ? &st::macosPlayerListOver : &st::macosPlayerListDesktopOver);
		_repeatToggle->setRippleColorOverride(nullptr);
		_repeatToggle->setAccessibleName(tr::lng_media_saved_music_title(tr::now));
		return;
	}
	_repeatToggle->setAccessibleName(tr::lng_schedule_repeat_label(tr::now));
	switch (Core::App().settings().playerRepeatMode()) {
	case RepeatMode::None:
		_repeatToggle->setIconOverride(
			&st::mediaPlayerRepeatDisabledIcon,
			&st::mediaPlayerRepeatDisabledIconOver);
		_repeatToggle->setRippleColorOverride(
			&st::mediaPlayerRepeatDisabledRippleBg);
		break;
	case RepeatMode::One:
		_repeatToggle->setIconOverride(&st::mediaPlayerRepeatOneIcon);
		_repeatToggle->setRippleColorOverride(nullptr);
		break;
	case RepeatMode::All:
		_repeatToggle->setIconOverride(nullptr);
		_repeatToggle->setRippleColorOverride(nullptr);
		break;
	}
}

void Widget::checkForTypeChange() {
	auto hasActiveType = [](AudioMsgId::Type type) {
		const auto current = instance()->current(type);
		const auto state = instance()->getState(type);
		return (current == state.id && !IsStoppedOrStopping(state.state));
	};
	if (hasActiveType(AudioMsgId::Type::Voice)) {
		_voiceIsActive = true;
		setType(AudioMsgId::Type::Voice);
	} else if (!_voiceIsActive && hasActiveType(AudioMsgId::Type::Song)) {
		setType(AudioMsgId::Type::Song);
	}
}

bool Widget::hasPlaybackSpeedControl() const {
	return _lastSongId.changeablePlaybackSpeed()
		&& Media::Audio::SupportsSpeedControl();
}

void Widget::updateControlsVisibility() {
	_repeatToggle->setVisible(_type == AudioMsgId::Type::Song);
	_orderToggle->setVisible(_type == AudioMsgId::Type::Song && !Jel::Ui::MacosInterfaceEnabled());
	_speedToggle->setVisible(hasPlaybackSpeedControl());
	if (Jel::Ui::MacosInterfaceEnabled() ? !_shadowSuppressed : !_shadow->isHidden()) {
		_playbackSlider->setVisible(_type == AudioMsgId::Type::Song || _type == AudioMsgId::Type::Voice);
	}
	updateControlsGeometry();
}

void Widget::setType(AudioMsgId::Type type) {
	if (_type != type) {
		_type = type;
		updateRepeatToggleIcon();
		handleSongChange();
		updateControlsVisibility();
		updateLabelsGeometry();
		handleSongUpdate(instance()->getState(_type));
		updateOverLabelsState(_labelsOver);
		_playlistChangesLifetime = instance()->playlistChanges(
			_type
		) | rpl::on_next([=] {
			handlePlaylistUpdate();
		});
		// maybe the type change causes a change of the button layout
		QResizeEvent event = { size(), size() };
		resizeEvent(&event);
	}
}

void Widget::handleSongUpdate(const TrackState &state) {
	checkForTypeChange();
	if (state.id.type() != _type || !state.id.audio()) {
		return;
	}

	if (Jel::Ui::MacosInterfaceEnabled()) {
		static_cast<PlaybackSlider*>(_playbackSlider.data())->setBufferedRanges(
			instance()->bufferedRanges(_type));
	}
	if (state.id.audio()->loading()) {
		_playbackProgress->updateLoadingState(state.id.audio()->progress());
	} else {
		_playbackProgress->updateState(state);
	}

	auto showPause = ShowPauseIcon(state.state);
	if (instance()->isSeeking(_type)) {
		showPause = true;
	}
	_playPause->setState(state.id.audio()->loading()
		? PlayButton::State::Cancel
		: showPause
		? PlayButton::State::Pause
		: PlayButton::State::Play);
	_playPause->setAccessibleName(showPause
		? tr::lng_shortcuts_media_pause(tr::now)
		: tr::lng_shortcuts_media_play(tr::now));

	updateTimeText(state);
}

void Widget::updateTimeText(const TrackState &state) {
	qint64 display = 0;
	const auto frequency = state.frequency;
	const auto document = state.id.audio();
	if (!IsStoppedOrStopping(state.state)) {
		display = state.position;
	} else if (state.length) {
		display = state.length;
	} else if (document->song()) {
		display = (document->duration() * frequency) / 1000;
	}

	_lastDurationMs = frequency > 0 ? (state.length * 1000LL) / frequency : 0;

	if (document->loading()) {
		const auto progress = document->progress() * 100;
		_time = QString::number(int(base::SafeRound(progress))) + '%';
		_playbackSlider->setDisabled(true);
	} else {
		display = frequency > 0 ? display / frequency : 0;
		_time = Ui::FormatDurationText(display);
		_playbackSlider->setDisabled(false);
	}
	static_cast<PlaybackSlider*>(_playbackSlider.data())->setDuration(_lastDurationMs);
	if (_seekPositionMs < 0) {
		updateTimeLabel();
	}
}

void Widget::updateTimeLabel() {
	auto timeLabelWidth = _timeLabel->width();
	if (_seekPositionMs >= 0) {
		auto playAlready = _seekPositionMs / 1000LL;
		_timeLabel->setText(Ui::FormatDurationText(playAlready));
	} else {
		_timeLabel->setText(_time);
	}
	if (timeLabelWidth != _timeLabel->width()) {
		updateLabelsGeometry();
	}
}

void Widget::handleSongChange() {
	const auto current = instance()->current(_type);
	const auto document = current.audio();
	_lastSongFromAnotherSession = document
		&& (document->session().uniqueId()
			!= _controller->session().uniqueId());
	if (!current
		|| !document
		|| ((_lastSongId.audio() == document)
			&& (_lastSongId.contextId() == current.contextId()))) {
		return;
	}
	_lastSongId = current;
	_speedController->reloadFromLookup();

	auto textWithEntities = TextWithEntities();
	if (document->isVoiceMessage() || document->isVideoMessage()) {
		textWithEntities = Ui::Text::FormatVoiceName(
			document,
			current.contextId()).textWithEntities(true);
	} else {
		textWithEntities = Ui::Text::FormatSongNameFor(document)
			.textWithEntities(true);
	}
	_hasArtist = false;
	_artistLabel->hide();
	if (Jel::Ui::MacosInterfaceEnabled()) {
		if (const auto song = document->song()) {
			if (!song->title.isEmpty() && !song->performer.isEmpty()) {
				textWithEntities = TextWithEntities{ song->title };
				_artistLabel->setText(song->performer);
				_hasArtist = true;
				_artistLabel->show();
			}
		}
	}
	_nameLabel->setMarkedText(textWithEntities);
	handlePlaylistUpdate();
	updateLabelsGeometry();
}

void Widget::handlePlaylistUpdate() {
	const auto previousEnabled = instance()->previousAvailable(_type);
	const auto nextEnabled = instance()->nextAvailable(_type);
	const auto nativeSong = Jel::Ui::MacosInterfaceEnabled() && _type == AudioMsgId::Type::Song;
	if (!previousEnabled && !nextEnabled && !nativeSong) {
		destroyPrevNextButtons();
	} else {
		createPrevNextButtons();
		if (Jel::Ui::MacosInterfaceEnabled()) {
			_previousTrack->setDisabled(!previousEnabled);
			_nextTrack->setDisabled(!nextEnabled);
		}
		_previousTrack->setIconOverride(previousEnabled ? nullptr : &st::mediaPlayerPreviousDisabledIcon);
		_previousTrack->setRippleColorOverride(previousEnabled ? nullptr : &st::mediaPlayerBg);
		_previousTrack->setPointerCursor(previousEnabled);
		_nextTrack->setIconOverride(nextEnabled ? nullptr : &st::mediaPlayerNextDisabledIcon);
		_nextTrack->setRippleColorOverride(nextEnabled ? nullptr : &st::mediaPlayerBg);
		_nextTrack->setPointerCursor(nextEnabled);
	}
}

void Widget::createPrevNextButtons() {
	if (!_previousTrack) {
		_previousTrack.create(this, PlayerSkipStyle(true));
		_previousTrack->show();
		_previousTrack->setClickedCallback([=]() {
			instance()->previous(_type);
		});
		_previousTrack->setAccessibleName(tr::lng_shortcuts_media_previous(tr::now));
		_nextTrack.create(this, PlayerSkipStyle(false));
		_nextTrack->show();
		_nextTrack->setClickedCallback([=]() {
			instance()->next(_type);
		});
		_nextTrack->setAccessibleName(tr::lng_shortcuts_media_next(tr::now));
		hidePlaylistOn(_previousTrack);
		hidePlaylistOn(_nextTrack);
		updatePlayPrevNextPositions();
	}
}

void Widget::destroyPrevNextButtons() {
	if (_previousTrack) {
		_previousTrack.destroy();
		_nextTrack.destroy();
		updatePlayPrevNextPositions();
	}
}

} // namespace Player
} // namespace Media
