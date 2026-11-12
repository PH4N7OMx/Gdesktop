#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QSet>
#include <QtCore/QTimer>

#include <deque>
#include <functional>
#include <memory>

namespace JellyPlugins::Client {

enum class RuntimeState { Stopped, Starting, Activating, Ready, Stopping, Failed };

struct RuntimeCallbacks {
	std::function<void(RuntimeState, const QString&)> state;
	std::function<void(const QJsonObject&)> frame;
};

class Supervisor final : public QObject {
public:
	explicit Supervisor(RuntimeCallbacks callbacks);
	~Supervisor();
	[[nodiscard]] bool start(const QString &host, const QString &isolationKey, quint64 generation,
		const QJsonObject &package, const QJsonObject &settings, QString &error);
	[[nodiscard]] bool send(QJsonObject frame);
	void stop();
	void awaitingUser(int requestId, bool waiting);
	[[nodiscard]] RuntimeState state() const;
	[[nodiscard]] const QString &instanceId() const;
	[[nodiscard]] quint64 generation() const;

private:
	void receive();
	void consume(const QJsonObject &frame);
	void pump();
	void tick();
	void fail(const QString &error);
	void transition(RuntimeState state, const QString &message = {});
	void deliver(const QJsonObject &frame);

	RuntimeCallbacks _callbacks;
	QProcess _process;
	std::shared_ptr<void> _sandbox;
	QTimer _watchdog;
	QElapsedTimer _clock;
	QByteArray _input;
	QByteArray _output;
	QByteArray _stderr;
	std::deque<QByteArray> _queue;
	qint64 _queuedBytes = 0;
	QJsonObject _init;
	QSet<int> _awaitingUser;
	RuntimeState _state = RuntimeState::Stopped;
	QString _instanceId;
	quint64 _generation = 0;
	qint64 _lastTick = 0;
	qint64 _launchDeadline = 0;
	qint64 _turnDeadline = 0;
	qint64 _stopDeadline = 0;
	qint64 _activationRemaining = 0;
	qint64 _frameWindow = 0;
	int _frames = 0;
	int _turn = 0;
	bool _inFlight = false;
	bool _hello = false;

};

} // namespace JellyPlugins::Client
