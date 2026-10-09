#pragma once

#include <QObject>
#include <QString>

namespace GummyPlugins {

class WorkerBridge final : public QObject {
	Q_OBJECT

public:
	Q_INVOKABLE void post(const QString &json);

};

[[nodiscard]] int RunWorker(int argc, char *argv[]);

} // namespace GummyPlugins
