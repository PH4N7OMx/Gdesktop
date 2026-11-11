/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_panel.h"

#include "styles/palette.h"

#include "media/player/media_player_instance.h"
#include "media/player/media_player_button.h"
#include "media/media_common.h"
#include "media/audio/media_audio.h"
#include "jel/ui/macos_interface.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "base/event_filter.h"
#include "base/weak_qptr.h"
#include "lang/lang_keys.h"
#include "info/media/info_media_list_widget.h"
#include "info/saved/info_saved_music_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_saved_music.h"
#include "ui/text/text_utilities.h"
#include "ui/image/image.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/format_values.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum_topic.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/scroll_area.h"
#include "ui/cached_round_corners.h"
#include "ui/ui_utility.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/window_controller.h"
#include "styles/style_overview.h"
#include "styles/style_media_player.h"
#include "styles/style_info.h"
#include "styles/style_jel_styles.h"

#include <QtWidgets/QApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainterPath>
#include <QtGui/QMoveEvent>

#include <array>

namespace Media {
namespace Player {
namespace {

using ListWidget = Info::Media::ListWidget;

[[nodiscard]] const style::MediaPlayerPlayButton &PlaylistPlayStyle() {
	static const auto result = [] {
		auto copy = st::mediaPlayerPlayButton;
		copy.size = style::ConvertScale(QSize(40, 40));
		copy.iconPosition = QPoint((copy.size.width() - copy.icon.size.width()) / 2,
			(copy.size.height() - copy.icon.size.height()) / 2);
		copy.color = st::windowFg;
		copy.rippleAreaPosition = QPoint();
		copy.rippleAreaSize = style::ConvertScale(40);
		return copy;
	}();
	return result;
}

class PlaylistControls final : public Ui::RpWidget {
public:
	PlaylistControls(
		QWidget *parent,
		Fn<void(not_null<Main::Session*>, QString)> searchArtistCallback)
	: RpWidget(parent)
	, _order(this, st::macosPlaylistButton)
	, _previous(this, st::macosPlaylistButton)
	, _play(this, PlaylistPlayStyle())
	, _next(this, st::macosPlaylistButton)
	, _repeat(this, st::macosPlaylistButton)
	, _title(this, st::macosPlayerName)
	, _artist(this, QString(), st::macosPlaylistArtist)
	, _progress(this, st::macosPlaylistSeek)
	, _played(this, st::macosPlaylistTime)
	, _remaining(this, st::macosPlaylistTime)
	, _add(this, tr::jel_PlayerAddToProfile(), st::macosPlaylistAdd)
	, _savedNotice(this, st::macosPlaylistSavedNotice)
	, _searchArtist(std::move(searchArtistCallback)) {
		_add->setFullRadius(false);
		_add->setCornerRadii(style::ConvertScale(10), style::ConvertScale(10),
			style::ConvertScale(10), style::ConvertScale(10));
		_add->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		_add->hide();
		_savedNotice->hide();
		_savedNotice->setSelectable(false);
		_add->setClickedCallback([=] {
			const auto document = _displayed.audio();
			if (document && document->isMusicForProfile()) {
				document->owner().savedMusic().save(document, _displayed.contextId());
			}
		});
		setFocusProxy(_play);
		const auto buttons = std::array<Ui::AbstractButton*, 7>{
			_order.data(), _previous.data(), _play.data(), _next.data(), _repeat.data(), _add.data(), _artist.data(),
		};
		for (const auto button : buttons) {
			button->setFocusPolicy(Qt::StrongFocus);
		}
		resize(width(), style::ConvertScale(140));
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
		_artist->setClickedCallback([=] { searchArtist(); });
		_played->setAttribute(Qt::WA_TransparentForMouseEvents);
		_remaining->setAttribute(Qt::WA_TransparentForMouseEvents);
		_progress->setChangeProgressCallback([=](float64 value) {
			const auto document = _displayed.audio();
			if (!_duration || !document || document->loading()) return;
			updateTimes(crl::time(value * _duration));
			if (!_seeking) {
				_seeking = true;
				instance()->startSeeking(AudioMsgId::Type::Song);
			}
		});
		_progress->setChangeFinishedCallback([=](float64 value) {
			if (_seeking) {
				_seeking = false;
				instance()->finishSeeking(AudioMsgId::Type::Song, value);
			}
		});
		rpl::single(rpl::empty) | rpl::then(style::PaletteChanged()) | rpl::on_next([=] {
			auto inactive = st::windowSubTextFg->c;
			inactive.setAlphaF(0.2);
			auto received = st::windowSubTextFg->c;
			received.setAlphaF(0.6);
			_progress->setColorOverrides({
				.activeFg = st::windowActiveTextFg->c,
				.seekFg = st::windowActiveTextFg->c,
				.inactiveFg = inactive,
				.receivedFg = received,
			});
			update();
		}, lifetime());
		if (Jel::Ui::MacosInterfaceIconsEnabled()) {
			_play->enableMacosIcons();
		}
		_previous->setAccessibleName(tr::lng_shortcuts_media_previous(tr::now));
		_next->setAccessibleName(tr::lng_shortcuts_media_next(tr::now));
		_previous->setClickedCallback([] { instance()->previous(AudioMsgId::Type::Song); });
		_next->setClickedCallback([] { instance()->next(AudioMsgId::Type::Song); });
		_play->setClickedCallback([] { instance()->playPauseCancelClicked(AudioMsgId::Type::Song); });
		_order->setClickedCallback([] {
			auto &settings = Core::App().settings();
			const auto mode = settings.playerOrderMode();
			settings.setPlayerOrderMode(mode == OrderMode::Default
				? OrderMode::Reverse : mode == OrderMode::Reverse
				? OrderMode::Shuffle : OrderMode::Default);
			Core::App().saveSettingsDelayed();
		});
		_repeat->setClickedCallback([] {
			auto &settings = Core::App().settings();
			const auto mode = settings.playerRepeatMode();
			settings.setPlayerRepeatMode(mode == RepeatMode::None
				? RepeatMode::All : mode == RepeatMode::All
				? RepeatMode::One : RepeatMode::None);
			Core::App().saveSettingsDelayed();
		});
		rpl::merge(Core::App().settings().playerOrderModeValue() | rpl::to_empty,
			Core::App().settings().playerRepeatModeValue() | rpl::to_empty
		) | rpl::on_next([=] { updateModes(); }, lifetime());
		rpl::merge(instance()->playlistChanges(AudioMsgId::Type::Song),
			instance()->trackChanged() | rpl::to_empty
		) | rpl::on_next([=] { updatePlayback(); }, lifetime());
		instance()->updatedNotifier(
		) | rpl::filter([](const TrackState &state) {
			return state.id.type() == AudioMsgId::Type::Song;
		}) | rpl::on_next([=] { updatePlayback(); }, lifetime());
		updatePlayback();
		_play->finishTransform();
	}

	[[nodiscard]] bool preventAutoHide() const {
		return _progress->isChanging() || _play->isDown() || _add->isDown()
			|| _previous->isDown() || _next->isDown()
			|| _order->isDown() || _repeat->isDown() || _artist->isDown();
	}

protected:
	void hideEvent(QHideEvent *e) override {
		finishProfileTransition();
		_progress->finishChanging();
		RpWidget::hideEvent(e);
	}

	void resizeEvent(QResizeEvent *e) override {
		const auto top = style::ConvertScale(75) + _remaining->height();
		const auto inset = style::ConvertScale(10);
		const auto textLeft = style::ConvertScale(60);
		const auto textWidth = std::max(0, width() - textLeft - inset);
		_title->resizeToNaturalWidth(textWidth);
		_artist->resizeToNaturalWidth(textWidth);
		const auto textInset = std::clamp((style::ConvertScale(40)
			- _title->height() - _artist->height()) / 2, 0, style::ConvertScale(2));
		_title->moveToLeft(textLeft, style::ConvertScale(10) + textInset);
		_artist->moveToLeft(textLeft, style::ConvertScale(50) - textInset - _artist->height());
		_progress->setGeometry(inset, style::ConvertScale(60),
			std::max(0, width() - 2 * inset), style::ConvertScale(12));
		_played->moveToLeft(inset, style::ConvertScale(75));
		_remaining->moveToRight(inset, style::ConvertScale(75));
		const auto gap = style::ConvertScale(10);
		const auto center = (width() - _play->width()) / 2;
		_order->moveToLeft(inset, top);
		_previous->moveToLeft(center - gap - _previous->width(), top);
		_play->moveToLeft(center, top);
		_next->moveToLeft(center + _play->width() + gap, top);
		_repeat->moveToRight(inset, top);
		const auto footerWidth = std::max(0, width() - 2 * inset);
		_add->setFullWidth(footerWidth);
		_add->moveToLeft(inset, style::ConvertScale(140));
		_savedNotice->resizeToWidth(footerWidth);
		_savedNotice->moveToLeft(inset, style::ConvertScale(140));
		const auto footerHeight = _profileEligible
			? (_inProfile ? _savedNotice->height() : _add->height()) + style::ConvertScale(15)
			: 0;
		const auto shownFooterHeight = _footerAnimating
			? int(base::SafeRound(_footerFromHeight
				+ (footerHeight - _footerFromHeight) * _footerProgress))
			: footerHeight;
		const auto targetHeight = style::ConvertScale(140) + shownFooterHeight;
		if (height() != targetHeight) resize(width(), targetHeight);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto cover = QRectF(style::ConvertScale(10), style::ConvertScale(10),
			style::ConvertScale(40), style::ConvertScale(40));
		auto path = QPainterPath();
		const auto radius = style::ConvertScale(6);
		path.addRoundedRect(cover, radius, radius);
		p.save();
		p.setClipPath(path);
		p.fillRect(cover, st::windowBgOver);
		const auto thumbnail = _media ? _media->thumbnail() : nullptr;
		if (thumbnail) {
			const auto image = thumbnail->original();
			const auto side = std::min(image.width(), image.height());
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.drawImage(cover, image, QRectF((image.width() - side) / 2.,
				(image.height() - side) / 2., side, side));
		} else if (Jel::Ui::MacosInterfaceIconsEnabled()) {
			st::macosPlaylistPlaceholder.paintInCenter(p, cover.toRect());
		} else {
			st::macosPlaylistPlaceholderDesktop.paintInCenter(p, cover.toRect());
		}
		p.restore();
		if (_footerAnimating) {
			const auto top = style::ConvertScale(140);
			p.setOpacity(1. - _footerProgress);
			p.drawPixmap(0, top, _footerFrom);
			p.setOpacity(_footerProgress);
			p.drawPixmap(0, top, _footerTo);
		}
	}

private:
	void searchArtist();
	void finishProfileTransition();

	void updateTimes(crl::time position) {
		position = std::clamp(position, crl::time(0), _duration);
		_played->setText(Ui::FormatDurationText(position / 1000));
		_remaining->setText(Ui::FormatDurationText((_duration - position) / 1000));
		QResizeEvent event(size(), size());
		resizeEvent(&event);
	}

	void updateTrack(const AudioMsgId &id) {
		if (_displayed == id) return;
		_displayed = id;
		_seeking = false;
		_mediaLifetime.destroy();
		_media = nullptr;
		const auto document = id.audio();
		if (!document) {
			_title->setText(QString());
			_artist->setText(QString());
			_artist->hide();
			updateProfile();
			update();
			return;
		}
		const auto song = document->song();
		_title->setText(song && !song->title.isEmpty()
			? song->title : document->filename());
		_artist->setText(song ? song->performer : QString());
		_artist->setVisible(song && !song->performer.trimmed().isEmpty());
		_media = document->createMediaView();
		_media->thumbnailWanted(id.contextId());
		document->owner().savedMusic().changed(
		) | rpl::filter([=](PeerId peerId) {
			return peerId == document->session().userPeerId();
		}) | rpl::on_next([=] { updateProfile(); }, _mediaLifetime);
		document->owner().savedMusic().loadIds();
		updateProfile();
		document->session().downloaderTaskFinished(
		) | rpl::on_next([=] { update(); }, _mediaLifetime);
		update();
	}

	void updateProfile() {
		const auto document = _displayed.audio();
		const auto eligible = document && document->isMusicForProfile();
		const auto saved = eligible && document->owner().savedMusic().has(document);
		const auto animate = _profileTrack == _displayed && !isHidden()
			&& window()->isActiveWindow() && !anim::Disabled()
			&& (eligible != _profileEligible || saved != _inProfile);
		const auto top = style::ConvertScale(140);
		const auto oldHeight = std::max(0, height() - top);
		const auto outgoing = animate && oldHeight
			? Ui::GrabWidget(this, QRect(0, top, width(), oldHeight)) : QPixmap();
		finishProfileTransition();
		_profileTrack = _displayed;
		_profileEligible = eligible;
		_inProfile = saved;
		_add->setVisible(_profileEligible && !_inProfile);
		_savedNotice->setVisible(_profileEligible && _inProfile);
		if (_inProfile) {
			auto text = TextWithEntities{ tr::lng_saved_music_added(tr::now) };
			text.append(u" "_q).append(Ui::Text::Link(tr::jel_PlayerRemoveFromProfile(tr::now), 1));
			_savedNotice->setMarkedText(text);
			const auto track = _displayed;
			const auto sessionId = document->session().uniqueId();
			const auto weak = base::make_weak(this);
			_savedNotice->setLink(1, std::make_shared<LambdaClickHandler>([=] {
				if (!weak || _displayed != track) return;
				const auto document = _displayed.audio();
				if (document && document->session().uniqueId() == sessionId
					&& document->isMusicForProfile()) {
					document->owner().savedMusic().remove(document);
				}
			}));
		}
		QResizeEvent event(size(), size());
		resizeEvent(&event);
		if (animate) {
			const auto newHeight = std::max(0, height() - top);
			_footerTo = newHeight
				? Ui::GrabWidget(this, QRect(0, top, width(), newHeight)) : QPixmap();
			_footerFrom = outgoing;
			_footerFromHeight = oldHeight;
			_footerProgress = 0.;
			_footerAnimating = true;
			_add->hide();
			_savedNotice->hide();
			_footerAnimation.start([=](float64 progress) {
				_footerProgress = progress;
				if (progress >= 1.) finishProfileTransition();
				QResizeEvent event(size(), size());
				resizeEvent(&event);
				update();
			}, 0., 1., 200, anim::easeOutCirc);
		}
	}

	void updatePlayback() {
		constexpr auto type = AudioMsgId::Type::Song;
		const auto nativeIcons = Jel::Ui::MacosInterfaceIconsEnabled();
		const auto previous = instance()->previousAvailable(type);
		const auto next = instance()->nextAvailable(type);
		_previous->setDisabled(!previous);
		_next->setDisabled(!next);
		_previous->setIconOverride(nativeIcons
			? (previous ? &st::macosPlaylistPrevious : &st::macosPlaylistPreviousDisabled)
			: (previous ? &st::mediaPlayerPreviousButton.icon : &st::mediaPlayerPreviousDisabledIcon));
		_next->setIconOverride(nativeIcons
			? (next ? &st::macosPlaylistNext : &st::macosPlaylistNextDisabled)
			: (next ? &st::mediaPlayerNextButton.icon : &st::mediaPlayerNextDisabledIcon));
		const auto state = instance()->getState(type);
		const auto document = state.id.audio();
		updateTrack(state.id);
		_progress->setBufferedRanges(instance()->bufferedRanges(type));
		_duration = state.frequency > 0
			? std::max<crl::time>(0, state.length * 1000LL / state.frequency) : 0;
		_progress->setDisabled(!document || !_duration || document->loading());
		const auto position = state.frequency > 0 ? state.position * 1000LL / state.frequency : 0;
		if (!_progress->isChanging()) {
			_progress->setValue(_duration ? position / float64(_duration) : 0.);
		}
		updateTimes(_progress->isChanging() ? crl::time(_progress->value() * _duration) : position);
		const auto showTimes = document && _duration && !IsStoppedOrStopping(state.state);
		_played->setVisible(showTimes);
		_remaining->setVisible(showTimes);
		const auto pause = ShowPauseIcon(state.state) || instance()->isSeeking(type);
		_play->setDisabled(!document);
		_play->setState(document && document->loading() ? PlayButton::State::Cancel
			: pause ? PlayButton::State::Pause : PlayButton::State::Play);
		_play->setAccessibleName(pause ? tr::lng_shortcuts_media_pause(tr::now)
			: tr::lng_shortcuts_media_play(tr::now));
	}

	void updateModes() {
		const auto nativeIcons = Jel::Ui::MacosInterfaceIconsEnabled();
		const auto &settings = Core::App().settings();
		const auto order = settings.playerOrderMode();
		_order->setIconOverride(order == OrderMode::Default
			? (nativeIcons ? &st::macosPlaylistOrder : &st::mediaPlayerReverseDisabledIcon)
			: order == OrderMode::Reverse
			? (nativeIcons ? &st::macosPlaylistReverse : &st::mediaPlayerReverseIcon)
			: (nativeIcons ? &st::macosPlaylistShuffle : &st::mediaPlayerShuffleIcon));
		_order->setAccessibleName(tr::lng_sr_playback_order(tr::now));
		_order->setAccessibleDescription(order == OrderMode::Default
			? tr::lng_settings_notifications_display_default(tr::now)
			: order == OrderMode::Reverse ? tr::lng_audio_player_reverse(tr::now)
			: tr::lng_audio_player_shuffle(tr::now));
		const auto repeat = settings.playerRepeatMode();
		_repeat->setIconOverride(repeat == RepeatMode::None
			? (nativeIcons ? &st::macosPlaylistRepeatNone : &st::mediaPlayerRepeatDisabledIcon)
			: repeat == RepeatMode::One
			? (nativeIcons ? &st::macosPlaylistRepeatOne : &st::mediaPlayerRepeatOneIcon)
			: (nativeIcons ? &st::macosPlaylistRepeatAll : &st::mediaPlayerRepeatButton.icon));
		_repeat->setAccessibleName(tr::lng_schedule_repeat_label(tr::now));
		_repeat->setAccessibleDescription(repeat == RepeatMode::None
			? tr::lng_schedule_repeat_never(tr::now)
			: repeat == RepeatMode::One ? QString::number(1)
			: tr::lng_manage_peer_reactions_on(tr::now));
	}

	object_ptr<Ui::IconButton> _order;
	object_ptr<Ui::IconButton> _previous;
	object_ptr<PlayButton> _play;
	object_ptr<Ui::IconButton> _next;
	object_ptr<Ui::IconButton> _repeat;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::LinkButton> _artist;
	object_ptr<Ui::MediaSlider> _progress;
	object_ptr<Ui::LabelSimple> _played;
	object_ptr<Ui::LabelSimple> _remaining;
	object_ptr<Ui::RoundButton> _add;
	object_ptr<Ui::FlatLabel> _savedNotice;
	bool _profileEligible = false;
	bool _inProfile = false;
	crl::time _duration = 0;
	bool _seeking = false;
	AudioMsgId _displayed;
	AudioMsgId _profileTrack;
	Ui::Animations::Simple _footerAnimation;
	QPixmap _footerFrom;
	QPixmap _footerTo;
	int _footerFromHeight = 0;
	float64 _footerProgress = 1.;
	bool _footerAnimating = false;
	std::shared_ptr<Data::DocumentMedia> _media;
	rpl::lifetime _mediaLifetime;
	Fn<void(not_null<Main::Session*>, QString)> _searchArtist;

};

void PlaylistControls::finishProfileTransition() {
	_footerAnimation.stop();
	_footerAnimating = false;
	_footerProgress = 1.;
	_footerFrom = _footerTo = QPixmap();
	_add->setVisible(_profileEligible && !_inProfile);
	_savedNotice->setVisible(_profileEligible && _inProfile);
}

void PlaylistControls::searchArtist() {
	const auto document = _displayed.audio();
	const auto song = document ? document->song() : nullptr;
	if (!song || song->performer.trimmed().isEmpty()) {
		return;
	}
	const auto callback = _searchArtist;
	callback(&document->session(), song->performer);
}

constexpr auto kPlaylistIdsLimit = 32;
constexpr auto kDelayedHideTimeout = crl::time(1800);

} // namespace

Panel::Panel(
	QWidget *parent,
	not_null<Window::SessionController*> window)
: RpWidget(parent)
, AbstractController(window)
, _showTimer([=] { startShow(); })
, _hideTimer([=] { startHideChecked(); })
, _scroll(this, st::mediaPlayerScroll) {
	hide();
	if (Jel::Ui::MacosInterfaceEnabled()) {
		_controls = object_ptr<PlaylistControls>(this, [=](
				not_null<Main::Session*> session,
				QString query) {
			searchArtist(session, std::move(query));
		});
		_controls->show();
		_controls->heightValue() | rpl::on_next([=] {
			updateSize();
			updateControlsGeometry();
		}, lifetime());
		base::install_event_filter(this, parentWidget(), [=](not_null<QEvent*> event) {
			if (event->type() == QEvent::Resize) {
				InvokeQueued(this, [=] { updateSize(); });
			}
			return base::EventFilterResult::Continue;
		});
		base::install_event_filter(this, qApp, [=](not_null<QEvent*> event) {
			if (!_pinned || isHidden()) return base::EventFilterResult::Continue;
			if (event->type() == QEvent::MouseButtonPress) {
				const auto position = static_cast<QMouseEvent*>(event.get())->globalPos();
				if (!rect().contains(mapFromGlobal(position)) && !_buttonAnchor.contains(position)) {
					hideIgnoringEnterEvents();
				}
			} else if (event->type() == QEvent::KeyPress
				&& static_cast<QKeyEvent*>(event.get())->key() == Qt::Key_Escape) {
				hideIgnoringEnterEvents();
				return base::EventFilterResult::Cancel;
			} else if (event->type() == QEvent::ApplicationDeactivate
				|| event->type() == QEvent::WindowDeactivate) {
				hideIgnoringEnterEvents();
			}
			return base::EventFilterResult::Continue;
		});
	}
	updateSize();
}

void Panel::searchArtist(not_null<Main::Session*> session, QString query) {
	const auto weakWindow = base::make_weak(&parentController()->window());
	const auto weakSession = base::make_weak(session);
	const auto sessionId = session->uniqueId();
	hideIgnoringEnterEvents();
	if (!weakWindow || !weakSession) {
		return;
	}
	weakWindow->invokeForSessionController(
		&weakSession->account(),
		nullptr,
		[query = std::move(query), sessionId](
				not_null<Window::SessionController*> controller) {
			if (controller->session().uniqueId() != sessionId) {
				return;
			}
			const auto weak = base::make_weak(controller);
			controller->searchMessages(query, Dialogs::Key());
			if (weak) {
				weak->window().activate();
			}
		});
}

bool Panel::overlaps(const QRect &globalRect) {
	if (isHidden() || _a_appearance.animating()) return false;

	auto marginLeft = rtl() ? contentRight() : contentLeft();
	auto marginRight = rtl() ? contentLeft() : contentRight();
	return rect().marginsRemoved(QMargins(marginLeft, contentTop(), marginRight, contentBottom())).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
}

void Panel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Panel::moveEvent(QMoveEvent *e) {
	RpWidget::moveEvent(e);
	if (_controls) updateSize();
}

void Panel::listHeightUpdated(int newHeight) {
	if (_controls || newHeight > emptyInnerHeight()) {
		updateSize();
	} else {
		_hideTimer.callOnce(0);
	}
}

bool Panel::contentTooSmall() const {
	if (_controls && instance()->current(AudioMsgId::Type::Song).audio()) {
		return false;
	}
	const auto innerHeight = _scroll->widget()
		? _scroll->widget()->height()
		: emptyInnerHeight();
	return (innerHeight <= emptyInnerHeight());
}

int Panel::emptyInnerHeight() const {
	return st::infoMediaMargin.top()
		+ st::overviewFileLayout.songPadding.top()
		+ st::overviewFileLayout.songThumbSize
		+ st::overviewFileLayout.songPadding.bottom()
		+ st::infoMediaMargin.bottom();
}

bool Panel::preventAutoHide() const {
	if (_controls && static_cast<PlaylistControls*>(_controls.data())->preventAutoHide()) {
		return true;
	}
	if (const auto list = static_cast<ListWidget*>(_scroll->widget())) {
		return list->preventAutoHide();
	}
	return false;
}

void Panel::updateControlsGeometry() {
	const auto width = contentWidth();
	if (_controls) {
		_controls->setGeometryToLeft(contentLeft(), contentTop(), width, _controls->height());
	}
	const auto scrollTop = contentTop() + (_controls ? _controls->height() : 0);
	const auto scrollHeight = std::max(
		height() - scrollTop - contentBottom() - scrollMarginBottom(),
		0);
	if (scrollHeight > 0) {
		_scroll->setGeometryToRight(contentRight(), scrollTop, width, scrollHeight);
		if (_controls) {
			const auto radius = style::ConvertScale(16);
			auto clip = QPainterPath();
			clip.setFillRule(Qt::WindingFill);
			clip.addRoundedRect(QRectF(_scroll->rect()), radius, radius);
			clip.addRect(QRectF(0., 0., width, std::min(radius, scrollHeight)));
			_scroll->setMask(QRegion(clip.simplified().toFillPolygon().toPolygon()));
		}
	}
	if (const auto widget = static_cast<RpWidget*>(_scroll->widget())) {
		widget->resizeToWidth(width);
	}
}

int Panel::bestPositionFor(int left) const {
	left -= contentLeft();
	left -= st::mediaPlayerFileLayout.songPadding.left();
	left -= st::mediaPlayerFileLayout.songThumbSize / 2;
	return left;
}

void Panel::scrollPlaylistToCurrentTrack() {
	if (const auto list = static_cast<ListWidget*>(_scroll->widget())) {
		const auto rect = list->getCurrentSongGeometry();
		_scroll->scrollToY(rect.y() - st::infoMediaMargin.top());
	}
}

void Panel::updateSize() {
	auto width = contentLeft() + st::mediaPlayerPanelWidth + contentRight();
	auto height = contentTop() + (_controls ? _controls->height() : 0);
	auto listHeight = 0;
	if (auto widget = _scroll->widget()) {
		listHeight = widget->height();
	}
	auto scrollVisible = (listHeight > 0);
	auto scrollHeight = scrollVisible
		? (std::min(listHeight, st::mediaPlayerListHeightMax)
			+ st::mediaPlayerListMarginBottom)
		: 0;
	if (_controls) {
		const auto available = std::max(0, parentWidget()->height() - y()
			- height - contentBottom());
		scrollHeight = std::min(scrollHeight, available);
	}
	height += scrollHeight + contentBottom();
	resize(width, height);
	_scroll->setVisible(scrollVisible);
}

style::color Panel::listBackground() const {
	return _controls ? st::windowBg : st::menuBg;
}

void Panel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	if (!_cache.isNull()) {
		bool animating = _a_appearance.animating();
		if (animating) {
			p.setOpacity(_a_appearance.value(_hiding ? 0. : 1.));
		} else if (_hiding || isHidden()) {
			hideFinished();
			return;
		}
		p.drawPixmap(0, 0, _cache);
		if (!animating) {
			showChildren();
			if (_pinned && _controls && window()->isActiveWindow()) {
				_controls->setFocus(Qt::PopupFocusReason);
			}
			_cache = QPixmap();
		}
		return;
	}

	// draw shadow
	auto shadowedRect = myrtlrect(contentLeft(), contentTop(), contentWidth(), contentHeight());
	auto shadowedSides = (rtl() ? RectPart::Right : RectPart::Left)
		| RectPart::Bottom
		| (rtl() ? RectPart::Left : RectPart::Right)
		| RectPart::Top;
	Ui::Shadow::paint(p, shadowedRect, width(), st::roundShadowRadius8px, shadowedSides);
	if (_controls) {
		p.setRenderHint(QPainter::Antialiasing);
		const auto dark = st::windowBg->c.lightnessF() < 0.5;
		auto rim = dark ? QColor(Qt::white) : QColor(Qt::black);
		rim.setAlphaF(dark ? 0.10 : 0.04);
		p.setPen(QPen(rim, style::ConvertScale(0.5)));
		p.setBrush(st::windowBg);
		const auto radius = style::ConvertScale(16);
		p.drawRoundedRect(shadowedRect, radius, radius);
	} else {
		Ui::FillRoundRect(p, shadowedRect, st::menuBg, Ui::MenuCorners);
	}
}

void Panel::enterEventHook(QEnterEvent *e) {
	if (_ignoringEnterEvents || contentTooSmall()) return;

	_hideTimer.cancel();
	if (_a_appearance.animating()) {
		startShow();
	} else {
		_showTimer.callOnce(0);
	}
	return RpWidget::enterEventHook(e);
}

void Panel::leaveEventHook(QEvent *e) {
	if (_pinned || preventAutoHide()) {
		return;
	}
	_showTimer.cancel();
	if (_a_appearance.animating()) {
		startHide();
	} else {
		_hideTimer.callOnce(300);
	}
	return RpWidget::leaveEventHook(e);
}

void Panel::showFromOther() {
	_hideTimer.cancel();
	if (_a_appearance.animating()) {
		startShow();
	} else {
		_showTimer.callOnce(300);
	}
}

void Panel::hideFromOther() {
	if (_pinned) return;
	_showTimer.cancel();
	if (_a_appearance.animating()) {
		startHide();
	} else {
		_hideTimer.callOnce(0);
	}
}

void Panel::toggleFromButton(QRect anchor) {
	if (_pinned && !isHidden() && !_hiding) {
		hideIgnoringEnterEvents();
		return;
	}
	_buttonAnchor = anchor;
	_pinned = true;
	_showTimer.cancel();
	_hideTimer.cancel();
	startShow();
	if (_controls && !isHidden()) {
		_controls->setFocus(Qt::PopupFocusReason);
	}
}

void Panel::ensureCreated() {
	if (_scroll->widget()) return;
	_refreshListLifetime.destroy();

	_refreshListLifetime = instance()->playlistChanges(
		AudioMsgId::Type::Song
	) | rpl::on_next([=] {
		refreshList();
	});
	refreshList();

	macWindowDeactivateEvents(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::on_next([=] {
		if (_pinned) {
			hideIgnoringEnterEvents();
		} else {
			leaveEvent(nullptr);
		}
	}, _refreshListLifetime);

	_ignoringEnterEvents = false;
}

void Panel::refreshList() {
	const auto current = instance()->current(AudioMsgId::Type::Song);
	const auto contextId = current.contextId();
	const auto context = instance()->playlistContext(AudioMsgId::Type::Song);
	auto savedMusicItem = false;
	const auto peer = [&]() -> PeerData* {
		if (const auto document = current.audio()) {
			if (&document->session() != &session()) {
				// Different account is playing music.
				return nullptr;
			}
		}
		const auto item = contextId
			? session().data().message(contextId)
			: nullptr;
		const auto media = item ? item->media() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (!document
			|| !document->isSharedMediaMusic()
			|| !IsRealPlaybackContext(item)) {
			return nullptr;
		}
		savedMusicItem = item->isSavedMusicItem();
		const auto result = item->history()->peer;
		if (const auto migrated = result->migrateTo()) {
			return migrated;
		}
		return result;
	}();
	const auto migrated = peer ? peer->migrateFrom() : nullptr;
	const auto listPeer = savedMusicItem ? nullptr : peer;
	const auto listMusicPeer = savedMusicItem ? peer : nullptr;
	const auto listTopicRootId = savedMusicItem
		? MsgId()
		: context.topicRootId;
	const auto listSublistPeerId = savedMusicItem
		? PeerId()
		: context.monoforumPeerId;
	const auto scoped = listTopicRootId || listSublistPeerId;
	const auto listMigratedPeer = (savedMusicItem || scoped)
		? nullptr
		: migrated;
	if (_listPeer != listPeer
		|| _listMusicPeer != listMusicPeer
		|| _listMigratedPeer != listMigratedPeer
		|| _listTopicRootId != listTopicRootId
		|| _listSublistPeerId != listSublistPeerId) {
		_scroll->takeWidget<QWidget>().destroy();
		_listPeer = _listMusicPeer = _listMigratedPeer = nullptr;
		_listTopicRootId = MsgId();
		_listSublistPeerId = PeerId();
	}
	if ((listPeer && !_listPeer) || (listMusicPeer && !_listMusicPeer)) {
		_listPeer = listPeer;
		_listMusicPeer = listMusicPeer;
		_listMigratedPeer = listMigratedPeer;
		_listTopicRootId = listTopicRootId;
		_listSublistPeerId = listSublistPeerId;
		auto list = object_ptr<ListWidget>(this, infoController());

		const auto weak = _scroll->setOwnedWidget(std::move(list));

		updateSize();
		updateControlsGeometry();

		weak->checkForHide(
		) | rpl::on_next([this] {
			if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
				_hideTimer.callOnce(kDelayedHideTimeout);
			}
		}, weak->lifetime());

		weak->heightValue(
		) | rpl::on_next([this](int newHeight) {
			listHeightUpdated(newHeight);
		}, weak->lifetime());

		weak->scrollToRequests(
		) | rpl::on_next([this](int newScrollTop) {
			_scroll->scrollToY(newScrollTop);
		}, weak->lifetime());

		using namespace rpl::mappers;
		rpl::combine(
			_scroll->scrollTopValue(),
			_scroll->heightValue(),
			tuple(_1, _1 + _2)
		) | rpl::on_next([=](int top, int bottom) {
			weak->setVisibleTopBottom(top, bottom);
		}, weak->lifetime());

		const auto type = listMusicPeer
			? Storage::SharedMediaType::MusicFile
			: section().mediaType();
		const auto topic = listTopic();
		const auto sublist = listSublist();
		auto musicMemento = Info::Saved::MusicMemento(peer);
		auto mediaMemento = topic
			? std::make_unique<Info::Media::Memento>(topic, type)
			: sublist
			? std::make_unique<Info::Media::Memento>(sublist, type)
			: std::make_unique<Info::Media::Memento>(
				peer,
				migratedPeerId(),
				type);
		auto &memento = listMusicPeer
			? musicMemento.media()
			: *mediaMemento;
		memento.setAroundId(contextId);
		memento.setIdsLimit(kPlaylistIdsLimit);
		memento.setScrollTopItem({ contextId, peer->session().uniqueId() });
		memento.setScrollTopShift(-st::infoMediaMargin.top());
		weak->restoreState(&memento);
	}
	if (_controls && !_scroll->widget()) {
		updateSize();
		updateControlsGeometry();
	}
}

void Panel::performDestroy() {
	if (_scroll->widget()) {
		_scroll->takeWidget<QWidget>().destroy();
	}
	_listPeer = _listMusicPeer = _listMigratedPeer = nullptr;
	_listTopicRootId = MsgId();
	_listSublistPeerId = PeerId();
	_refreshListLifetime.destroy();
}

Data::ForumTopic *Panel::listTopic() const {
	return (_listPeer && _listTopicRootId)
		? _listPeer->forumTopicFor(_listTopicRootId)
		: nullptr;
}

Data::SavedSublist *Panel::listSublist() const {
	return _listPeer
		? _listPeer->monoforumSublistFor(_listSublistPeerId)
		: nullptr;
}

Info::Key Panel::key() const {
	if (_listMusicPeer) {
		return Info::Key(Info::Saved::MusicTag{ _listMusicPeer });
	} else if (const auto topic = listTopic()) {
		return Info::Key(topic);
	} else if (const auto sublist = listSublist()) {
		return Info::Key(sublist);
	}
	return Info::Key(_listPeer);
}

PeerData *Panel::migrated() const {
	return _listMigratedPeer;
}

Info::Section Panel::section() const {
	return _listMusicPeer
		? Info::Section(Info::Section::Type::SavedMusic)
		: Info::Section(Info::Section::MediaType::MusicFile);
}

void Panel::startShow() {
	ensureCreated();
	if (contentTooSmall()) {
		return;
	}

	if (isHidden()) {
		scrollPlaylistToCurrentTrack();
		show();
	} else if (!_hiding) {
		return;
	}
	_hiding = false;
	startAnimation();
}

void Panel::hideIgnoringEnterEvents() {
	_pinned = false;
	_ignoringEnterEvents = true;
	if (isHidden()) {
		hideFinished();
	} else {
		startHide();
	}
}

void Panel::startHideChecked() {
	if (_pinned) return;
	if (!contentTooSmall() && preventAutoHide()) {
		return;
	}
	if (isHidden()) {
		hideFinished();
	} else {
		startHide();
	}
}

void Panel::startHide() {
	if (_hiding || isHidden()) return;

	_hiding = true;
	startAnimation();
}

void Panel::startAnimation() {
	auto from = _hiding ? 1. : 0.;
	auto to = _hiding ? 0. : 1.;
	if (_cache.isNull()) {
		showChildren();
		_cache = Ui::GrabWidget(this);
	}
	hideChildren();
	_a_appearance.start([this] { appearanceCallback(); }, from, to, st::defaultInnerDropdown.duration);
}

void Panel::appearanceCallback() {
	if (!_a_appearance.animating() && _hiding) {
		_hiding = false;
		hideFinished();
	} else {
		update();
	}
}

void Panel::hideFinished() {
	_pinned = false;
	hide();
	_cache = QPixmap();
	performDestroy();
}

int Panel::contentLeft() const {
	return st::mediaPlayerPanelMarginLeft;
}

int Panel::contentTop() const {
	return st::mediaPlayerPanelMarginLeft;
}

int Panel::contentRight() const {
	return st::mediaPlayerPanelMarginLeft;
}

int Panel::contentBottom() const {
	return st::mediaPlayerPanelMarginBottom;
}

int Panel::scrollMarginBottom() const {
	return 0;// st::mediaPlayerPanelMarginBottom;
}

} // namespace Player
} // namespace Media
