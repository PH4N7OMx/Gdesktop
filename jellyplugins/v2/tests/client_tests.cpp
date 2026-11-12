#include "client/contract.h"
#include "client/scheduler.h"
#include "client/store.h"
#include "client/supervisor.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>

#include <cstdio>
#include <cstdlib>

namespace {

using namespace JellyPlugins::Client;

void Check(bool condition, const char *message) {
	if (condition) return;
	std::fprintf(stderr, "FAIL: %s\n", message);
	std::abort();
}

bool Wait(const std::function<bool()> &condition, int milliseconds = 8000) {
	auto clock = QElapsedTimer();
	clock.start();
	while (!condition() && clock.elapsed() < milliseconds) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return condition();
}

Action Send(QString text = QStringLiteral("first"), bool silent = false, QString chat = QStringLiteral("-123")) {
	auto action = Action();
	auto error = QString();
	Check(PrepareAction(QStringLiteral("telegram.sendText"), {
		{ QStringLiteral("chatId"), chat }, { QStringLiteral("text"), text }, { QStringLiteral("silent"), silent },
	}, action, error), "send action validation");
	return action;
}

void Policy() {
	const auto action = Send();
	const auto exact = QJsonArray{ MakeRule(action, true) };
	const auto broad = QJsonArray{ MakeRule(action, true, true) };
	Check(ValidRules(exact) && ValidRules(broad), "rules validate");
	for (const auto global : { AccessMode::Ask, AccessMode::Remember, AccessMode::Full }) {
		for (const auto local : { AccessMode::Inherit, AccessMode::Ask, AccessMode::Remember, AccessMode::Full }) {
			const auto mode = local == AccessMode::Inherit ? global : local;
			Check(Evaluate(global, local, Revision(), exact, action) == (mode == AccessMode::Ask ? Decision::Ask : Decision::Allow), "mode inheritance matrix");
			Check(Evaluate(global, local, Revision(), { MakeRule(action, false), MakeRule(action, true) }, action) == Decision::Deny, "denial overrides all modes and allowances");
		}
	}
	Check(Evaluate(AccessMode::Remember, AccessMode::Inherit, Revision(), exact, Send(QStringLiteral("changed"))) == Decision::Ask, "changed text requires approval");
	Check(Evaluate(AccessMode::Remember, AccessMode::Inherit, Revision(), broad, Send(QStringLiteral("changed"))) == Decision::Allow, "explicit broad text grant");
	Check(Evaluate(AccessMode::Remember, AccessMode::Inherit, Revision(), broad, Send(QStringLiteral("changed"), true)) == Decision::Ask, "broad scope preserves silent flag");
	Check(Evaluate(AccessMode::Remember, AccessMode::Inherit, Revision(), broad, Send(QStringLiteral("changed"), false, QStringLiteral("-456"))) == Decision::Ask, "broad scope preserves recipient");
	Check(Evaluate(AccessMode::Full, AccessMode::Full, QStringLiteral("old-contract"), {}, action) == Decision::Ask, "API changes require new full-access consent");
	auto read = Action();
	auto error = QString();
	Check(PrepareAction(QStringLiteral("telegram.readHistory"), { { QStringLiteral("chatId"), QStringLiteral("-123") } }, read, error), "read action validates");
	Check(read.protectedData && Evaluate(AccessMode::Remember, AccessMode::Inherit, Revision(), broad, read) == Decision::Ask, "reading protected data requires its own grant");
	Check(!PrepareAction(QStringLiteral("telegram.sendText"), { { QStringLiteral("chatId"), QStringLiteral("-123") },
		{ QStringLiteral("text"), QStringLiteral("test") }, { QStringLiteral("replyTo"), 1 } }, read, error), "unknown action field cannot inherit permission");
	Check(!PrepareAction(QStringLiteral("telegram.readHistory"), { { QStringLiteral("chatId"), QStringLiteral("-123\n") } }, read, error), "chat ids cannot hide trailing data");
	Check(!PrepareAction(QStringLiteral("native.exec"), {}, read, error), "native operations unavailable");
	std::puts("Policy: mode matrix, protected reads, exact action and denial checks passed");
}

void Persistence() {
	auto directory = QTemporaryDir();
	Check(directory.isValid(), "temporary state directory");
	auto store = Store(directory.path());
	auto error = QString();
	const auto initial = QJsonObject{ { QStringLiteral("schema"), 1 }, { QStringLiteral("storage"), QJsonObject{ { QStringLiteral("value"), 1 } } } };
	Check(store.write(QStringLiteral("test.plugin"), initial, error), "initial atomic state write");
	const auto next = QJsonObject{ { QStringLiteral("schema"), 1 }, { QStringLiteral("previous"), initial }, { QStringLiteral("version"), 2 } };
	Check(store.write(QStringLiteral("test.plugin"), next, error), "atomic package/state snapshot update");
	Check(store.read(QStringLiteral("test.plugin"), error) == next, "snapshot stays coherent");
	Check(!store.write(QStringLiteral("../escape"), initial, error), "store path traversal denied");
	auto file = QFile(directory.path() + QStringLiteral("/damaged.plugin.json"));
	Check(file.open(QIODevice::WriteOnly), "corrupt fixture open");
	Check(file.write("{broken") == 7, "corrupt fixture write");
	file.close();
	Check(store.read(QStringLiteral("damaged.plugin"), error).isEmpty() && !error.isEmpty(), "corrupt state is reported");
	Check(!store.write(QStringLiteral("damaged.plugin"), initial, error), "corrupt state cannot be silently overwritten");
	Check(store.remove(QStringLiteral("test.plugin"), error), "explicit scoped plugin removal");
	Check(QFile::exists(file.fileName()), "other records survive removal");
	std::puts("Persistence: atomic snapshots, corrupt-state preservation and scoped removal passed");
}

void Scheduling() {
	auto scheduler = Scheduler();
	Check(scheduler.every(QStringLiteral("task"), 60, false, 0), "schedule registration");
	Check(scheduler.due(59999).isEmpty(), "first interval waits for its period");
	Check(scheduler.due(60000).size() == 1, "first due interval");
	Check(scheduler.due(3600000).isEmpty(), "long pending action never overlaps itself");
	scheduler.complete(QStringLiteral("task"), 3600000);
	Check(scheduler.due(3600000).isEmpty(), "missed intervals do not burst");
	Check(scheduler.due(3660000).size() == 1, "next normal interval after resume");
	scheduler.cancel(QStringLiteral("task"));
	Check(scheduler.due(7200000).isEmpty(), "canceled schedule never resumes");
	Check(scheduler.every(QStringLiteral("once"), 60, true, 10), "explicit immediate registration");
	Check(scheduler.due(10).size() == 1, "explicit immediate first event");
	std::puts("Scheduler: first delay, monotonic ticks, no overlap and no missed-event burst passed");
}

QJsonObject Package(const QString &code) {
	return { { QStringLiteral("formatVersion"), 2 }, { QStringLiteral("manifest"), QJsonObject{
		{ QStringLiteral("id"), QStringLiteral("test.supervisor") }, { QStringLiteral("name"), QStringLiteral("Test") },
		{ QStringLiteral("version"), QStringLiteral("2.0.0") }, { QStringLiteral("apiVersion"), 2 }, { QStringLiteral("capabilities"), QJsonArray() },
	} }, { QStringLiteral("code"), code } };
}

void Runtime(const QString &path, const QString &stalled) {
	auto error = QString();
	auto request = QJsonObject();
	auto state = RuntimeState::Stopped;
	auto supervisor = std::unique_ptr<Supervisor>();
	supervisor = std::make_unique<Supervisor>(RuntimeCallbacks{
		.state = [&](RuntimeState value, const QString &message) { state = value; if (!message.isEmpty()) std::fprintf(stderr, "%s\n", message.toUtf8().constData()); },
		.frame = [&](const QJsonObject &frame) { request = frame; },
	});
	Check(supervisor->start(path, QStringLiteral("test.native-client"), 1,
		Package(QStringLiteral("export default async api => { await api.storage.get('startup'); };")), {}, error), error.toUtf8().constData());
	Check(Wait([&] { return request.value(QStringLiteral("type")) == QJsonValue(QStringLiteral("request")); }), "native activation request arrives");
	Check(supervisor->state() == RuntimeState::Activating, "transport acknowledgement is not readiness");
	const auto id = request.value(QStringLiteral("requestId")).toInt();
	supervisor->awaitingUser(id, true);
	Check(supervisor->send({ { QStringLiteral("type"), QStringLiteral("result") }, { QStringLiteral("requestId"), id }, { QStringLiteral("value"), QJsonValue(QJsonValue::Null) } }), "native API reply queued");
	supervisor->awaitingUser(id, false);
	Check(Wait([&] { return state == RuntimeState::Ready || state == RuntimeState::Failed; }) && state == RuntimeState::Ready, "ready follows completed async activation");
	supervisor->stop();
	Check(Wait([&] { return state == RuntimeState::Stopped; }), "native stop completes");
	auto failed = false;
	Supervisor broken({ .state = [&](RuntimeState value, const QString&) { failed = value == RuntimeState::Failed; } });
	Check(broken.start(path, QStringLiteral("test.native-loop"), 1, Package(QStringLiteral("export default () => { while (true) {} };")), {}, error), "loop host starts");
	Check(Wait([&] { return failed; }), "infinite execution is killed without hanging the client");
	failed = false;
	Supervisor hanging({ .state = [&](RuntimeState value, const QString&) { failed = value == RuntimeState::Failed; } });
	Check(hanging.start(stalled, QStringLiteral("test.native-stall"), 1, Package(QStringLiteral("export default () => {};")), {}, error), "stall fixture starts");
	Check(Wait([&] { return failed; }), "external watchdog kills a host that never acknowledges a turn");
	std::puts("Supervisor: real isolated host, asynchronous readiness, stop and external watchdog passed");
}

} // namespace

int main(int argc, char **argv) {
	auto application = QCoreApplication(argc, argv);
	Check(argc == 3, "host and stall-fixture paths required");
	Policy();
	Persistence();
	Scheduling();
	Runtime(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
	return 0;
}
