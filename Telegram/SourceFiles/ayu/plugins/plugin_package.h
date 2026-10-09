#pragma once

#include <QJsonObject>
#include <QStringList>

namespace GummyPlugins {

inline constexpr auto kApiVersion = 1;
inline constexpr auto kPackageLimit = 1024 * 1024;
inline constexpr auto kFrameLimit = 2 * 1024 * 1024;
inline constexpr auto kStorageLimit = 64 * 1024;

struct Permissions {
	QStringList readChats;
	QStringList sendChats;
	QStringList httpHosts;
	bool storage = false;
	bool timers = false;
	bool ui = false;
	int maxMessagesPerHour = 20;
};

struct Package {
	QString id;
	QString name;
	QString author;
	QString description;
	QString version;
	QString code;
	QString digest;
	Permissions permissions;
	QJsonObject json;
};

[[nodiscard]] bool ParsePackage(const QByteArray &data, Package &result, QString &error);
[[nodiscard]] bool ParsePermissions(const QJsonObject &json, Permissions &result, QString &error);
[[nodiscard]] QJsonObject PermissionsJson(const Permissions &permissions);
[[nodiscard]] bool IsSubset(const Permissions &grant, const Permissions &requested);

} // namespace GummyPlugins
