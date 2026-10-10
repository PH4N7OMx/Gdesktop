#include "ayu/plugins/plugin_worker.h"

#include "ayu/plugins/plugin_package.h"
#include "ayu/plugins/plugin_sandbox.h"

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJSValue>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace JellyPlugins {
namespace {

FILE *Input = nullptr;
FILE *Output = nullptr;

void WriteFrame(const QJsonObject &frame) {
	const auto bytes = QJsonDocument(frame).toJson(QJsonDocument::Compact);
	if (bytes.size() > kFrameLimit || !Output) {
		return;
	}
	if (std::fwrite(bytes.constData(), 1, bytes.size(), Output) != size_t(bytes.size())
		|| std::fputc('\n', Output) == EOF || std::fflush(Output) != 0) {
		std::_Exit(73);
	}
}

class ExecutionDeadline final {
public:
	explicit ExecutionDeadline(QJSEngine &engine) : _thread([this, &engine] {
		auto lock = std::unique_lock(_mutex);
		if (!_condition.wait_for(lock, std::chrono::seconds(2), [this] { return _done; })) {
			engine.setInterrupted(true);
		}
	}) {
	}
	~ExecutionDeadline() {
		{
			auto lock = std::lock_guard(_mutex);
			_done = true;
		}
		_condition.notify_one();
		_thread.join();
	}

private:
	std::mutex _mutex;
	std::condition_variable _condition;
	bool _done = false;
	std::thread _thread;

};

} // namespace

void WorkerBridge::post(const QString &json) {
	const auto bytes = json.toUtf8();
	if (bytes.size() > kFrameLimit) {
		std::_Exit(74);
	}
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isObject()) {
		std::_Exit(74);
	}
	WriteFrame(document.object());
}

int RunWorker(int argc, char *argv[]) {
	if (!IsSandboxWorker()) {
		return 77;
	}
	qputenv("QV4_FORCE_INTERPRETER", "1");
#ifdef Q_OS_WIN
	const auto in = _open_osfhandle(reinterpret_cast<intptr_t>(
		GetStdHandle(STD_INPUT_HANDLE)), _O_RDONLY | _O_BINARY);
	const auto out = _open_osfhandle(reinterpret_cast<intptr_t>(
		GetStdHandle(STD_OUTPUT_HANDLE)), _O_WRONLY | _O_BINARY);
	if (in < 0 || out < 0) {
		return 73;
	}
	Input = _fdopen(in, "rb");
	Output = _fdopen(out, "wb");
#else
	Input = stdin;
	Output = stdout;
#endif
	if (!Input || !Output) {
		return 73;
	}
	auto application = QCoreApplication(argc, argv);
	auto engine = QJSEngine();
	auto bridge = WorkerBridge();
	engine.globalObject().setProperty(u"__jellyNative"_q, engine.newQObject(&bridge));
	auto sdk = QFile(u":/jelly/plugins/runtime.js"_q);
	if (!sdk.open(QIODevice::ReadOnly)) {
		return 78;
	}
	const auto bootstrap = engine.evaluate(QString::fromUtf8(sdk.readAll()), u"jelly-sdk"_q);
	if (bootstrap.isError()) {
		return 78;
	}
	auto initialized = false;
	auto buffer = std::vector<char>(kFrameLimit + 2);
	while (std::fgets(buffer.data(), int(buffer.size()), Input)) {
		const auto line = QByteArray(buffer.data());
		if (!line.endsWith('\n') || line.size() > kFrameLimit) {
			return 74;
		}
		const auto frame = QJsonDocument::fromJson(line).object();
		if (frame.isEmpty()) {
			return 74;
		}
		auto deadline = ExecutionDeadline(engine);
		auto result = QJSValue();
		if (!initialized) {
			if (frame[u"type"_q].toString() != u"init"_q) {
				return 74;
			}
			auto package = Package();
			auto error = QString();
			if (!ParsePackage(QJsonDocument(frame[u"package"_q].toObject())
				.toJson(QJsonDocument::Compact), package, error)) {
				return 74;
			}
			engine.globalObject().setProperty(u"__jellyConfig"_q,
				engine.toScriptValue(frame[u"settings"_q].toObject().toVariantMap()));
			result = engine.evaluate(package.code, package.id + u".js"_q);
			if (!result.isError()) {
				result = engine.evaluate(
					u"__jellyStart(JellyPlugin.default, __jellyConfig);"_q, u"jelly-start"_q);
			}
			initialized = true;
		} else {
			result = engine.globalObject().property(u"__jellyDispatch"_q)
				.call({ engine.toScriptValue(frame.toVariantMap()) });
		}
		QCoreApplication::sendPostedEvents();
		QCoreApplication::processEvents();
		if (result.isError() || engine.isInterrupted()) {
			WriteFrame({ { u"type"_q, u"fatal"_q },
				{ u"error"_q, result.toString().left(500) } });
			return 70;
		}
		WriteFrame({ { u"type"_q, u"ack"_q } });
	}
	return 0;
}

} // namespace JellyPlugins
