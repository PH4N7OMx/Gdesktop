#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace JellyPlugins::Client {

enum class AccessMode { Inherit = -1, Ask = 0, Remember = 1, Full = 2 };
enum class Decision { Ask, Allow, Deny };

struct Package {
	QString id;
	QString name;
	QString version;
	QString author;
	QString description;
	QString code;
	QString digest;
	QJsonObject json;
};

struct Action {
	QString method;
	QString category;
	QJsonObject params;
	QString fingerprint;
	bool protectedData = false;
};

[[nodiscard]] const QJsonObject &Contract();
[[nodiscard]] int Limit(const char *name);
[[nodiscard]] QString Revision();
[[nodiscard]] bool ValidJson(const QJsonValue &value, int byteLimit);
[[nodiscard]] bool ParsePackage(const QByteArray &data, Package &result, QString &error);
[[nodiscard]] bool PrepareAction(const QString &method, const QJsonObject &params, Action &result, QString &error);
[[nodiscard]] bool ValidMode(int mode, bool inherit);
[[nodiscard]] Decision Evaluate(AccessMode global, AccessMode local, const QString &fullRevision, const QJsonArray &rules, const Action &action);
[[nodiscard]] QJsonObject MakeRule(const Action &action, bool allow, bool anyText = false);
[[nodiscard]] bool ValidRules(const QJsonArray &rules);

} // namespace JellyPlugins::Client
