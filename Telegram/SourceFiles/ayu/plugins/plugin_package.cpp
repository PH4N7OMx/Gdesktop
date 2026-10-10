#include "ayu/plugins/plugin_package.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace JellyPlugins {
namespace {

bool ParseList(const QJsonObject &json, const QString &key, QStringList &out, bool hosts) {
	if (!json.contains(key)) {
		return true;
	}
	if (!json[key].isArray() || json[key].toArray().size() > 32) {
		return false;
	}
	static const auto chat = QRegularExpression(u"^-?[1-9][0-9]{0,15}$"_q);
	static const auto host = QRegularExpression(
		u"^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+[a-z]{2,63}$"_q);
	for (const auto &value : json[key].toArray()) {
		const auto text = value.toString();
		if (!value.isString()
			|| !(hosts ? host : chat).match(text).hasMatch()
			|| (hosts && (text.endsWith(u".localhost"_q)
				|| text.endsWith(u".local"_q)
				|| text.endsWith(u".internal"_q)))) {
			return false;
		}
		if (!out.contains(text)) {
			out.push_back(text);
		}
	}
	return true;
}

bool ListSubset(const QStringList &subset, const QStringList &set) {
	for (const auto &item : subset) {
		if (!set.contains(item)) {
			return false;
		}
	}
	return true;
}

} // namespace

bool ParsePermissions(const QJsonObject &json, Permissions &result, QString &error) {
	const auto allowed = QStringList{
		u"readChats"_q, u"sendChats"_q, u"httpHosts"_q,
		u"joinChannels"_q, u"botChats"_q, u"attachmentChats"_q,
		u"editChats"_q, u"reactionChats"_q, u"historyChats"_q,
		u"storage"_q, u"timers"_q, u"ui"_q, u"maxMessagesPerHour"_q,
	};
	for (auto i = json.begin(); i != json.end(); ++i) {
		if (!allowed.contains(i.key())) {
			error = u"Unknown permission: "_q + i.key();
			return false;
		}
	}
	auto parsed = Permissions();
	if (!ParseList(json, u"readChats"_q, parsed.readChats, false)
		|| !ParseList(json, u"sendChats"_q, parsed.sendChats, false)
		|| !ParseList(json, u"joinChannels"_q, parsed.joinChannels, false)
		|| !ParseList(json, u"botChats"_q, parsed.botChats, false)
		|| !ParseList(json, u"attachmentChats"_q, parsed.attachmentChats, false)
		|| !ParseList(json, u"editChats"_q, parsed.editChats, false)
		|| !ParseList(json, u"reactionChats"_q, parsed.reactionChats, false)
		|| !ParseList(json, u"historyChats"_q, parsed.historyChats, false)
		|| !ParseList(json, u"httpHosts"_q, parsed.httpHosts, true)) {
		error = u"Invalid chat or HTTPS host scope."_q;
		return false;
	}
	for (const auto &key : { u"storage"_q, u"timers"_q, u"ui"_q }) {
		if (json.contains(key) && !json[key].isBool()) {
			error = u"Permission must be boolean: "_q + key;
			return false;
		}
	}
	parsed.storage = json[u"storage"_q].toBool();
	parsed.timers = json[u"timers"_q].toBool();
	parsed.ui = json[u"ui"_q].toBool();
	const auto limit = json.value(u"maxMessagesPerHour"_q).toDouble(20);
	if ((json.contains(u"maxMessagesPerHour"_q) && !json[u"maxMessagesPerHour"_q].isDouble())
		|| limit < 1 || limit > 60 || limit != int(limit)) {
		error = u"Message limit must be an integer from 1 to 60."_q;
		return false;
	}
	parsed.maxMessagesPerHour = int(limit);
	result = std::move(parsed);
	return true;
}

QJsonObject PermissionsJson(const Permissions &permissions) {
	return {
		{ u"readChats"_q, QJsonArray::fromStringList(permissions.readChats) },
		{ u"sendChats"_q, QJsonArray::fromStringList(permissions.sendChats) },
		{ u"joinChannels"_q, QJsonArray::fromStringList(permissions.joinChannels) },
		{ u"botChats"_q, QJsonArray::fromStringList(permissions.botChats) },
		{ u"attachmentChats"_q, QJsonArray::fromStringList(permissions.attachmentChats) },
		{ u"editChats"_q, QJsonArray::fromStringList(permissions.editChats) },
		{ u"reactionChats"_q, QJsonArray::fromStringList(permissions.reactionChats) },
		{ u"historyChats"_q, QJsonArray::fromStringList(permissions.historyChats) },
		{ u"httpHosts"_q, QJsonArray::fromStringList(permissions.httpHosts) },
		{ u"storage"_q, permissions.storage },
		{ u"timers"_q, permissions.timers },
		{ u"ui"_q, permissions.ui },
		{ u"maxMessagesPerHour"_q, permissions.maxMessagesPerHour },
	};
}

bool IsSubset(const Permissions &grant, const Permissions &requested) {
	return ListSubset(grant.readChats, requested.readChats)
		&& ListSubset(grant.sendChats, requested.sendChats)
		&& ListSubset(grant.joinChannels, requested.joinChannels)
		&& ListSubset(grant.botChats, requested.botChats)
		&& ListSubset(grant.attachmentChats, requested.attachmentChats)
		&& ListSubset(grant.editChats, requested.editChats)
		&& ListSubset(grant.reactionChats, requested.reactionChats)
		&& ListSubset(grant.historyChats, requested.historyChats)
		&& ListSubset(grant.httpHosts, requested.httpHosts)
		&& (!grant.storage || requested.storage)
		&& (!grant.timers || requested.timers)
		&& (!grant.ui || requested.ui)
		&& grant.maxMessagesPerHour <= requested.maxMessagesPerHour;
}

bool ParsePackage(const QByteArray &data, Package &result, QString &error) {
	if (data.isEmpty() || data.size() > kPackageLimit) {
		error = u"Package exceeds 1 MiB."_q;
		return false;
	}
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(data, &parseError);
	const auto json = document.object();
	const auto manifest = json[u"manifest"_q].toObject();
	static const auto idPattern = QRegularExpression(u"^[a-z][a-z0-9.-]{2,63}$"_q);
	static const auto versionPattern = QRegularExpression(u"^[0-9]+\\.[0-9]+\\.[0-9]+$"_q);
	if (parseError.error != QJsonParseError::NoError
		|| !json[u"manifest"_q].isObject()
		|| manifest[u"apiVersion"_q].toDouble() != kApiVersion
		|| !idPattern.match(manifest[u"id"_q].toString()).hasMatch()
		|| !versionPattern.match(manifest[u"version"_q].toString()).hasMatch()
		|| !manifest[u"permissions"_q].isObject()
		|| !json[u"code"_q].isString()) {
		error = u"Invalid JellyPlugin manifest or API version."_q;
		return false;
	}
	auto parsed = Package();
	parsed.id = manifest[u"id"_q].toString();
	parsed.name = manifest[u"name"_q].toString();
	parsed.author = manifest[u"author"_q].toString();
	parsed.description = manifest[u"description"_q].toString();
	parsed.version = manifest[u"version"_q].toString();
	parsed.code = json[u"code"_q].toString();
	if (parsed.name.isEmpty() || parsed.name.size() > 80
		|| parsed.author.size() > 80 || parsed.description.size() > 2000
		|| (manifest.contains(u"author"_q) && !manifest[u"author"_q].isString())
		|| (manifest.contains(u"description"_q) && !manifest[u"description"_q].isString())
		|| parsed.code.isEmpty()) {
		error = u"Invalid plugin metadata or empty code."_q;
		return false;
	}
	if (!ParsePermissions(manifest[u"permissions"_q].toObject(), parsed.permissions, error)) {
		return false;
	}
	parsed.json = json;
	parsed.digest = QString::fromLatin1(QCryptographicHash::hash(
		QJsonDocument(json).toJson(QJsonDocument::Compact),
		QCryptographicHash::Sha256).toHex());
	result = std::move(parsed);
	return true;
}

} // namespace JellyPlugins
