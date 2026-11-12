#include "supervisor.h"

#include "contract.h"
#include "launcher.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QUuid>

namespace JellyPlugins::Client {

Supervisor::Supervisor(RuntimeCallbacks callbacks) : _callbacks(std::move(callbacks)) {
	_watchdog.setInterval(50);
	QObject::connect(&_watchdog, &QTimer::timeout, this, [this] { tick(); });
	QObject::connect(&_process, &QProcess::readyReadStandardOutput, this, [this] { receive(); });
	QObject::connect(&_process, &QProcess::bytesWritten, this, [this](qint64) { pump(); });
	QObject::connect(&_process, &QProcess::readyReadStandardError, this, [this] {
		_stderr += _process.readAllStandardError();
		if (_stderr.size() > 4096) _stderr = _stderr.right(4096);
	});
	QObject::connect(&_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
		if (_state != RuntimeState::Stopping && _state != RuntimeState::Stopped && _state != RuntimeState::Failed) {
			fail(_process.errorString() + QStringLiteral(": ") + QString::fromUtf8(_stderr).left(500));
		}
	});
	QObject::connect(&_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
		if (_state == RuntimeState::Stopping) transition(RuntimeState::Stopped);
		else if (_state != RuntimeState::Failed && _state != RuntimeState::Stopped) {
			fail(QStringLiteral("Plugin host exited (%1): %2").arg(code).arg(QString::fromUtf8(_stderr).left(500)));
		}
	});
}

Supervisor::~Supervisor() {
	if (_process.state() != QProcess::NotRunning) {
		_process.kill();
		_process.waitForFinished(1000);
	}
}

bool Supervisor::start(const QString &host, const QString &isolationKey, quint64 generation,
		const QJsonObject &package, const QJsonObject &settings, QString &error) {
	if (_state != RuntimeState::Stopped || _clock.isValid() || !generation || generation > 9007199254740991ULL) {
		error = QStringLiteral("Invalid runtime generation or reused supervisor.");
		return false;
	}
	_sandbox = PrepareLaunch(_process, host, isolationKey, error);
	if (!_sandbox) return false;
	_generation = generation;
	_instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	_init = { { QStringLiteral("type"), QStringLiteral("init") }, { QStringLiteral("package"), package }, { QStringLiteral("settings"), settings } };
	_clock.start();
	_launchDeadline = Limit("startupMilliseconds");
	_activationRemaining = Limit("activationMilliseconds");
	transition(RuntimeState::Starting);
	_watchdog.start();
	_process.start();
	return true;
}

RuntimeState Supervisor::state() const { return _state; }
const QString &Supervisor::instanceId() const { return _instanceId; }
quint64 Supervisor::generation() const { return _generation; }

void Supervisor::transition(RuntimeState state, const QString &message) {
	_state = state;
	if (state == RuntimeState::Stopped || state == RuntimeState::Failed) _watchdog.stop();
	const auto callback = _callbacks.state;
	if (callback) QTimer::singleShot(0, this, [=] { callback(state, message); });
}

void Supervisor::deliver(const QJsonObject &frame) {
	const auto callback = _callbacks.frame;
	if (callback) QTimer::singleShot(0, this, [=] { callback(frame); });
}

void Supervisor::fail(const QString &error) {
	if (_state == RuntimeState::Failed || _state == RuntimeState::Stopped) return;
	_queue.clear();
	_queuedBytes = 0;
	_output.clear();
	transition(RuntimeState::Failed, error);
	if (_process.state() != QProcess::NotRunning) _process.kill();
}

bool Supervisor::send(QJsonObject frame) {
	if (!_hello || _state == RuntimeState::Stopped || _state == RuntimeState::Failed
		|| _queue.size() >= size_t(Limit("pendingRequests"))) return false;
	frame.insert(QStringLiteral("protocolVersion"), Contract().value(QStringLiteral("protocolVersion")));
	frame.insert(QStringLiteral("instanceId"), _instanceId);
	frame.insert(QStringLiteral("generation"), double(_generation));
	const auto bytes = QJsonDocument(frame).toJson(QJsonDocument::Compact);
	if (bytes.size() > Limit("frameBytes") || _queuedBytes + bytes.size() > Limit("frameBytes") * 2) return false;
	_queuedBytes += bytes.size();
	_queue.push_back(bytes);
	pump();
	return true;
}

void Supervisor::pump() {
	if (_state == RuntimeState::Stopped || _state == RuntimeState::Failed || _process.state() != QProcess::Running) return;
	if (!_inFlight && _output.isEmpty() && !_queue.empty()) {
		if (_turn == 2147483647) { fail(QStringLiteral("Runtime turn limit reached.")); return; }
		const auto bytes = std::move(_queue.front());
		_queue.pop_front();
		_queuedBytes -= bytes.size();
		const auto size = quint32(bytes.size());
		_output.resize(4);
		for (auto i = 0; i != 4; ++i) _output[i] = char(size >> (24 - 8 * i));
		_output += bytes;
		_inFlight = true;
		++_turn;
		_turnDeadline = _clock.elapsed() + Limit("transportMilliseconds");
	}
	if (!_output.isEmpty()) {
		const auto written = _process.write(_output);
		if (written < 0) fail(QStringLiteral("Cannot write plugin transport."));
		else _output.remove(0, int(written));
	}
}

void Supervisor::receive() {
	_input += _process.readAllStandardOutput();
	if (_input.size() > Limit("frameBytes") * 2) { fail(QStringLiteral("Plugin transport buffer exceeded.")); return; }
	while (_input.size() >= 4 && _state != RuntimeState::Failed) {
		auto size = quint32(0);
		for (auto i = 0; i != 4; ++i) size = (size << 8) | static_cast<unsigned char>(_input[i]);
		if (!size || size > quint32(Limit("frameBytes"))) { fail(QStringLiteral("Invalid plugin frame length.")); return; }
		if (_input.size() < qint64(size) + 4) return;
		const auto data = _input.mid(4, int(size));
		_input.remove(0, int(size) + 4);
		const auto document = QJsonDocument::fromJson(data);
		if (!document.isObject()) { fail(QStringLiteral("Invalid plugin JSON frame.")); return; }
		if (_clock.elapsed() - _frameWindow >= 1000) { _frameWindow = _clock.elapsed(); _frames = 0; }
		if (++_frames > Limit("framesPerTurn") * 2) { fail(QStringLiteral("Plugin output rate exceeded.")); return; }
		consume(document.object());
	}
}

void Supervisor::consume(const QJsonObject &frame) {
	const auto type = frame.value(QStringLiteral("type")).toString();
	if (frame.value(QStringLiteral("protocolVersion")) != Contract().value(QStringLiteral("protocolVersion"))) {
		fail(QStringLiteral("Plugin protocol version mismatch."));
		return;
	}
	if (type == QStringLiteral("fatal")) {
		const auto error = frame.value(QStringLiteral("error")).toObject();
		fail(error.value(QStringLiteral("code")).toString() + QStringLiteral(": ") + error.value(QStringLiteral("message")).toString().left(500));
		return;
	}
	if (!_hello) {
#ifdef Q_OS_WIN
		const auto sandbox = QStringLiteral("windows-appcontainer");
#elif defined(Q_OS_MAC)
		const auto sandbox = QStringLiteral("macos-app-sandbox");
#else
		const auto sandbox = QStringLiteral("linux-seccomp");
#endif
		if (type != QStringLiteral("hello") || frame.value(QStringLiteral("apiVersion")) != Contract().value(QStringLiteral("apiVersion"))
			|| frame.value(QStringLiteral("packageVersion")) != Contract().value(QStringLiteral("packageVersion"))
			|| frame.value(QStringLiteral("sandbox")).toString() != sandbox) { fail(QStringLiteral("Invalid plugin handshake or sandbox.")); return; }
		_hello = true;
		transition(RuntimeState::Activating);
		if (!send(_init)) fail(QStringLiteral("Cannot initialize plugin."));
		_init = {};
		return;
	}
	if (frame.value(QStringLiteral("instanceId")).toString() != _instanceId
		|| frame.value(QStringLiteral("generation")).toDouble() != double(_generation)) { fail(QStringLiteral("Stale plugin instance.")); return; }
	if (type == QStringLiteral("turnComplete")) {
		if (!_inFlight || frame.value(QStringLiteral("turnId")).toDouble() != _turn) { fail(QStringLiteral("Invalid plugin turn acknowledgement.")); return; }
		_inFlight = false;
		pump();
	} else if (type == QStringLiteral("ready")) {
		if (_state == RuntimeState::Stopping) return;
		if (_state != RuntimeState::Activating) { fail(QStringLiteral("Unexpected plugin ready state.")); return; }
		transition(RuntimeState::Ready);
	} else if (type == QStringLiteral("stopped")) {
		if (_state != RuntimeState::Stopping) fail(QStringLiteral("Unexpected plugin stop state."));
	} else if (type == QStringLiteral("request") || type == QStringLiteral("log") || type == QStringLiteral("eventDone")) {
		if (_state == RuntimeState::Activating || _state == RuntimeState::Ready) deliver(frame);
	} else {
		fail(QStringLiteral("Unknown plugin frame."));
	}
}

void Supervisor::awaitingUser(int requestId, bool waiting) {
	if (waiting) _awaitingUser.insert(requestId);
	else _awaitingUser.remove(requestId);
}

void Supervisor::stop() {
	if (_state == RuntimeState::Stopped || _state == RuntimeState::Stopping || _state == RuntimeState::Failed) return;
	_queue.clear();
	_queuedBytes = 0;
	_awaitingUser.clear();
	transition(RuntimeState::Stopping);
	_stopDeadline = _clock.elapsed() + Limit("stopMilliseconds");
	if (!_hello || !send({ { QStringLiteral("type"), QStringLiteral("stop") } })) _process.kill();
}

void Supervisor::tick() {
	const auto now = _clock.elapsed();
	const auto elapsed = now - _lastTick;
	_lastTick = now;
	if (_state == RuntimeState::Starting && now >= _launchDeadline) { fail(QStringLiteral("Plugin host startup timed out.")); return; }
	if (_state == RuntimeState::Stopping) {
		if (_process.state() == QProcess::NotRunning) { transition(RuntimeState::Stopped); return; }
		if (now >= _stopDeadline) _process.kill();
		return;
	}
	if (_inFlight && now >= _turnDeadline) { fail(QStringLiteral("Plugin execution or transport timed out.")); return; }
	if (_state == RuntimeState::Activating && _awaitingUser.isEmpty()) {
		_activationRemaining -= elapsed;
		if (_activationRemaining <= 0) fail(QStringLiteral("Plugin activation timed out."));
	}
}

} // namespace JellyPlugins::Client
