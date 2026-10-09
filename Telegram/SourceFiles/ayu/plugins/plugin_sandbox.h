#pragma once

#include <QProcess>
#include <QString>
#include <memory>

namespace GummyPlugins {

[[nodiscard]] std::shared_ptr<void> PrepareSandbox(
	QProcess &process,
	const QString &pluginId,
	QString &error);
[[nodiscard]] bool IsSandboxWorker();

} // namespace GummyPlugins
