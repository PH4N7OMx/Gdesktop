#pragma once

#include <QtCore/QProcess>
#include <QtCore/QString>

#include <memory>

namespace JellyPlugins::Client {

[[nodiscard]] QString HostPath();
[[nodiscard]] std::shared_ptr<void> PrepareLaunch(QProcess &process, const QString &path, const QString &isolationKey, QString &error);

} // namespace JellyPlugins::Client
