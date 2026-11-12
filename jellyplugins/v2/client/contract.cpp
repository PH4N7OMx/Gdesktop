#include "contract.h"

#include "contract.generated.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>

#include <cmath>
#include <functional>

namespace JellyPlugins::Client {
namespace {

bool Keys(const QJsonObject &object, const QStringList &allowed, const QStringList &required = {}) {
	for (auto i = object.begin(); i != object.end(); ++i) {
		if (!allowed.contains(i.key())) return false;
	}
	for (const auto &key : required) {
		if (!object.contains(key)) return false;
	}
	return true;
}

bool Matches(const QString &value, const QString &pattern) {
	const auto match = QRegularExpression(pattern).match(value);
	return match.hasMatch() && match.capturedLength() == value.size();
}

QString Fingerprint(const QJsonObject &object) {
	return QString::fromLatin1(QCryptographicHash::hash(
		QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

bool RuleMatches(const QJsonObject &rule, const Action &action) {
	if (rule.value(QStringLiteral("revision")).toString() != Revision()
		|| rule.value(QStringLiteral("method")).toString() != action.method) return false;
	if (rule.value(QStringLiteral("scope")).toString() == QStringLiteral("exact")) {
		return rule.value(QStringLiteral("fingerprint")).toString() == action.fingerprint;
	}
	return rule.value(QStringLiteral("scope")).toString() == QStringLiteral("chat-text")
		&& action.method == QStringLiteral("telegram.sendText")
		&& rule.value(QStringLiteral("chatId")) == action.params.value(QStringLiteral("chatId"))
		&& rule.value(QStringLiteral("silent")) == action.params.value(QStringLiteral("silent"));
}

} // namespace

const QJsonObject &Contract() {
	static const auto result = QJsonDocument::fromJson(kContractJson).object();
	return result;
}

int Limit(const char *name) {
	return Contract().value(QStringLiteral("limits")).toObject().value(QLatin1String(name)).toInt();
}

QString Revision() {
	return QString::fromLatin1(kContractRevision);
}

bool ValidJson(const QJsonValue &value, int byteLimit) {
	auto count = 0;
	const auto maximum = Limit("jsonNodes");
	auto walk = std::function<bool(const QJsonValue&, int)>();
	walk = [&](const QJsonValue &item, int depth) {
		if (++count > maximum || depth > Limit("jsonDepth") || item.isUndefined()) return false;
		if (item.isDouble()) return std::isfinite(item.toDouble());
		if (item.isArray()) {
			for (const auto &child : item.toArray()) if (!walk(child, depth + 1)) return false;
		} else if (item.isObject()) {
			const auto object = item.toObject();
			for (auto i = object.begin(); i != object.end(); ++i) if (!walk(i.value(), depth + 1)) return false;
		}
		return true;
	};
	if (!walk(value, 0)) return false;
	const auto encoded = QJsonDocument(QJsonArray{ value }).toJson(QJsonDocument::Compact);
	return encoded.size() - 2 <= byteLimit;
}

bool ParsePackage(const QByteArray &data, Package &result, QString &error) {
	error = QStringLiteral("Invalid JellyPlugins v2 package. Old packages are unsupported.");
	if (data.size() > Limit("packageBytes")) return false;
	const auto document = QJsonDocument::fromJson(data);
	if (!document.isObject()) return false;
	const auto json = document.object();
	if (!Keys(json, { QStringLiteral("formatVersion"), QStringLiteral("manifest"), QStringLiteral("code") })
		|| json.value(QStringLiteral("formatVersion")) != QJsonValue(Contract().value(QStringLiteral("packageVersion")))
		|| !json.value(QStringLiteral("manifest")).isObject()
		|| !json.value(QStringLiteral("code")).isString()
		|| !ValidJson(json, Limit("packageBytes"))) return false;
	const auto manifest = json.value(QStringLiteral("manifest")).toObject();
	if (!Keys(manifest, { QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("version"), QStringLiteral("apiVersion"),
		QStringLiteral("author"), QStringLiteral("description"), QStringLiteral("capabilities") },
		{ QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("version"), QStringLiteral("apiVersion"), QStringLiteral("capabilities") })
		|| manifest.value(QStringLiteral("apiVersion")) != Contract().value(QStringLiteral("apiVersion"))) return false;
	auto package = Package();
	package.id = manifest.value(QStringLiteral("id")).toString();
	package.name = manifest.value(QStringLiteral("name")).toString();
	package.version = manifest.value(QStringLiteral("version")).toString();
	package.author = manifest.value(QStringLiteral("author")).toString();
	package.description = manifest.value(QStringLiteral("description")).toString();
	package.code = json.value(QStringLiteral("code")).toString();
	if (!Matches(package.id, QStringLiteral("^[a-z][a-z0-9.-]{2,63}$"))
		|| !Matches(package.version, QStringLiteral("^[0-9]{1,6}\\.[0-9]{1,6}\\.[0-9]{1,6}$"))
		|| package.name.trimmed().isEmpty() || package.name.size() > 80
		|| package.code.trimmed().isEmpty() || package.code.contains(QChar(0))) return false;
	for (const auto &key : { QStringLiteral("author"), QStringLiteral("description") }) {
		if (manifest.contains(key) && (!manifest.value(key).isString()
			|| manifest.value(key).toString().size() > (key == QStringLiteral("author") ? 80 : 2000))) return false;
	}
	if (!manifest.value(QStringLiteral("capabilities")).isArray()) return false;
	auto categories = QSet<QString>();
	const auto methods = Contract().value(QStringLiteral("methods")).toObject();
	for (auto i = methods.begin(); i != methods.end(); ++i) categories.insert(i.value().toObject().value(QStringLiteral("category")).toString());
	auto seen = QSet<QString>();
	for (const auto &category : manifest.value(QStringLiteral("capabilities")).toArray()) {
		if (!category.isString() || !categories.contains(category.toString()) || seen.contains(category.toString())) return false;
		seen.insert(category.toString());
	}
	package.json = json;
	package.digest = Fingerprint(json);
	result = std::move(package);
	error.clear();
	return true;
}

bool PrepareAction(const QString &method, const QJsonObject &input, Action &result, QString &error) {
	error = QStringLiteral("INVALID_PARAMETER");
	const auto methods = Contract().value(QStringLiteral("methods")).toObject();
	if (!methods.contains(method)) {
		error = QStringLiteral("METHOD_UNAVAILABLE");
		return false;
	}
	const auto entry = methods.value(method).toObject();
	const auto schema = entry.value(QStringLiteral("params")).toObject();
	if (!Keys(input, schema.keys())) return false;
	auto params = QJsonObject();
	for (auto i = schema.begin(); i != schema.end(); ++i) {
		const auto field = i.value().toObject();
		const auto type = field.value(QStringLiteral("type")).toString();
		const auto value = input.contains(i.key()) ? input.value(i.key()) : field.value(QStringLiteral("default"));
		const auto number = value.toDouble();
		const auto valid = type == QStringLiteral("chat") ? value.isString() && Matches(value.toString(), QStringLiteral("^-?[1-9][0-9]{0,15}$"))
			: type == QStringLiteral("key") ? value.isString() && Matches(value.toString(), QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}$"))
			: type == QStringLiteral("string") ? value.isString() && value.toString().size() >= field.value(QStringLiteral("min")).toInt()
				&& value.toString().size() <= field.value(QStringLiteral("max")).toInt()
			: type == QStringLiteral("integer") ? value.isDouble() && std::isfinite(number) && std::floor(number) == number
				&& number >= field.value(QStringLiteral("min")).toDouble() && number <= field.value(QStringLiteral("max")).toDouble()
			: type == QStringLiteral("boolean") ? value.isBool()
			: type == QStringLiteral("enum") ? field.value(QStringLiteral("values")).toArray().contains(value)
			: type == QStringLiteral("json") && ValidJson(value, Limit("jsonBytes"));
		if (!valid) return false;
		params.insert(i.key(), value);
	}
	result = Action{ method, entry.value(QStringLiteral("category")).toString(), params,
		Fingerprint({ { QStringLiteral("revision"), Revision() }, { QStringLiteral("method"), method }, { QStringLiteral("params"), params } }),
		entry.value(QStringLiteral("access")).toString() == QStringLiteral("protected") };
	error.clear();
	return true;
}

bool ValidMode(int mode, bool inherit) {
	return mode >= (inherit ? -1 : 0) && mode <= 2;
}

Decision Evaluate(AccessMode global, AccessMode local, const QString &fullRevision, const QJsonArray &rules, const Action &action) {
	if (!action.protectedData) return Decision::Allow;
	auto allowed = false;
	for (const auto &item : rules) {
		const auto rule = item.toObject();
		if (!RuleMatches(rule, action)) continue;
		if (!rule.value(QStringLiteral("allow")).toBool()) return Decision::Deny;
		allowed = true;
	}
	auto mode = local == AccessMode::Inherit ? global : local;
	if (mode == AccessMode::Full && fullRevision != Revision()) mode = AccessMode::Ask;
	return mode == AccessMode::Full || (mode == AccessMode::Remember && allowed) ? Decision::Allow : Decision::Ask;
}

QJsonObject MakeRule(const Action &action, bool allow, bool anyText) {
	auto result = QJsonObject{ { QStringLiteral("revision"), Revision() }, { QStringLiteral("method"), action.method },
		{ QStringLiteral("allow"), allow }, { QStringLiteral("scope"), QStringLiteral("exact") },
		{ QStringLiteral("fingerprint"), action.fingerprint }, { QStringLiteral("params"), action.params } };
	if (anyText && action.method == QStringLiteral("telegram.sendText")) {
		result.insert(QStringLiteral("scope"), QStringLiteral("chat-text"));
		result.insert(QStringLiteral("chatId"), action.params.value(QStringLiteral("chatId")));
		result.insert(QStringLiteral("silent"), action.params.value(QStringLiteral("silent")));
	}
	return result;
}

bool ValidRules(const QJsonArray &rules) {
	if (rules.size() > Limit("savedRules") || !ValidJson(rules, 512 * 1024)) return false;
	for (const auto &item : rules) {
		if (!item.isObject()) return false;
		const auto rule = item.toObject();
		if (!rule.value(QStringLiteral("allow")).isBool() || !rule.value(QStringLiteral("revision")).isString()) return false;
		auto action = Action();
		auto error = QString();
		if (!PrepareAction(rule.value(QStringLiteral("method")).toString(), rule.value(QStringLiteral("params")).toObject(), action, error)) return false;
		const auto scope = rule.value(QStringLiteral("scope")).toString();
		if (scope != QStringLiteral("exact") && (scope != QStringLiteral("chat-text") || action.method != QStringLiteral("telegram.sendText"))) return false;
		if (rule.value(QStringLiteral("revision")).toString() == Revision() && !RuleMatches(rule, action)) return false;
	}
	return true;
}

} // namespace JellyPlugins::Client
