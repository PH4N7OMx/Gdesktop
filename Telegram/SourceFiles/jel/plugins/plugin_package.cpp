#include "jel/plugins/plugin_package.h"

#include <algorithm>
#include <cmath>
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
			|| (hosts ? host : chat).match(text).capturedLength() != text.size()
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

bool ValidSettingValue(const QJsonObject &field, const QJsonValue &value) {
	const auto type = field[u"type"_q].toString();
	if (type == u"boolean"_q) return value.isBool();
	if (type == u"string"_q) {
		return value.isString() && value.toString().size() <= field[u"maxLength"_q].toInt(512);
	} else if (type == u"number"_q) {
		return value.isDouble() && std::isfinite(value.toDouble())
			&& value.toDouble() >= field[u"min"_q].toDouble(-1e9)
			&& value.toDouble() <= field[u"max"_q].toDouble(1e9);
	} else if (type == u"select"_q) {
		return value.isString() && field[u"options"_q].toArray().contains(value);
	}
	return false;
}

bool ParseSettingsSchema(const QJsonValue &value, QJsonArray &out) {
	if (value.isUndefined()) return true;
	if (!value.isArray() || value.toArray().size() > 16
		|| QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact).size() > 4096) return false;
	static const auto keyPattern = QRegularExpression(u"^[a-z][a-zA-Z0-9_.-]{0,63}$"_q);
	auto keys = QStringList();
	for (const auto &row : value.toArray()) {
		if (!row.isObject()) return false;
		const auto field = row.toObject();
		const auto key = field[u"key"_q].toString();
		const auto label = field[u"label"_q].toString();
		const auto type = field[u"type"_q].toString();
		const auto allowed = QStringList{ u"key"_q, u"label"_q, u"type"_q, u"default"_q,
			u"min"_q, u"max"_q, u"maxLength"_q, u"options"_q };
		for (auto i = field.begin(); i != field.end(); ++i) {
			if (!allowed.contains(i.key())) return false;
		}
		if (!keyPattern.match(key).hasMatch() || keyPattern.match(key).capturedLength() != key.size()
			|| keys.contains(key) || label.isEmpty() || label.size() > 80) return false;
		keys.push_back(key);
		if (field.contains(u"maxLength"_q)) {
			const auto max = field[u"maxLength"_q].toInt();
			if (type != u"string"_q || !field[u"maxLength"_q].isDouble()
				|| max < 1 || max > 2000 || field[u"maxLength"_q].toDouble() != max) return false;
		}
		for (const auto &bound : { u"min"_q, u"max"_q }) {
			if (field.contains(bound) && (type != u"number"_q || !field[bound].isDouble()
				|| !std::isfinite(field[bound].toDouble()) || std::abs(field[bound].toDouble()) > 1e9)) return false;
		}
		if (field[u"min"_q].toDouble(-1e9) > field[u"max"_q].toDouble(1e9)) return false;
		if (type == u"select"_q) {
			if (!field[u"options"_q].isArray()) return false;
			const auto options = field[u"options"_q].toArray();
			auto seen = QStringList();
			if (options.isEmpty() || options.size() > 8) return false;
			for (const auto &option : options) {
				const auto text = option.toString();
				if (!option.isString() || text.isEmpty() || text.size() > 80 || seen.contains(text)) return false;
				seen.push_back(text);
			}
		} else if (field.contains(u"options"_q)) {
			return false;
		}
		if (!field.contains(u"default"_q) || !ValidSettingValue(field, field[u"default"_q])) return false;
	}
	out = value.toArray();
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
		u"webviewBots"_q, u"fileRead"_q, u"fileWrite"_q, u"moneyRead"_q,
		u"menuChats"_q, u"uiDialogs"_q,
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
		|| !ParseList(json, u"httpHosts"_q, parsed.httpHosts, true)
		|| !ParseList(json, u"menuChats"_q, parsed.menuChats, false)
		|| !ParseList(json, u"webviewBots"_q, parsed.webviewBots, false)
		|| std::any_of(parsed.webviewBots.begin(), parsed.webviewBots.end(),
			[](const QString &id) { return id.startsWith('-'); })) {
		error = u"Invalid chat or HTTPS host scope."_q;
		return false;
	}
	for (const auto &key : { u"storage"_q, u"timers"_q, u"ui"_q,
		u"fileRead"_q, u"fileWrite"_q, u"moneyRead"_q, u"uiDialogs"_q }) {
		if (json.contains(key) && !json[key].isBool()) {
			error = u"Permission must be boolean: "_q + key;
			return false;
		}
	}
	parsed.storage = json[u"storage"_q].toBool();
	parsed.timers = json[u"timers"_q].toBool();
	parsed.ui = json[u"ui"_q].toBool();
	parsed.fileRead = json[u"fileRead"_q].toBool();
	parsed.fileWrite = json[u"fileWrite"_q].toBool();
	parsed.moneyRead = json[u"moneyRead"_q].toBool();
	parsed.uiDialogs = json[u"uiDialogs"_q].toBool();
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
		{ u"fileRead"_q, permissions.fileRead },
		{ u"fileWrite"_q, permissions.fileWrite },
		{ u"moneyRead"_q, permissions.moneyRead },
		{ u"uiDialogs"_q, permissions.uiDialogs },
		{ u"menuChats"_q, QJsonArray::fromStringList(permissions.menuChats) },
		{ u"webviewBots"_q, QJsonArray::fromStringList(permissions.webviewBots) },
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
		&& (!grant.fileRead || requested.fileRead)
		&& (!grant.fileWrite || requested.fileWrite)
		&& (!grant.moneyRead || requested.moneyRead)
		&& (!grant.uiDialogs || requested.uiDialogs)
		&& ListSubset(grant.menuChats, requested.menuChats)
		&& ListSubset(grant.webviewBots, requested.webviewBots)
		&& grant.maxMessagesPerHour <= requested.maxMessagesPerHour;
}

QJsonObject SettingsWithDefaults(const Package &package, QJsonObject settings) {
	for (const auto &row : package.settingsSchema) {
		const auto field = row.toObject();
		const auto key = field[u"key"_q].toString();
		if (!settings.contains(key)) settings[key] = field[u"default"_q];
	}
	return settings;
}

bool ValidateSettings(const Package &package, const QJsonObject &settings, QString &error) {
	if (QJsonDocument(settings).toJson().size() > 8192) {
		error = u"Settings exceed 8 KiB."_q;
		return false;
	}
	for (const auto &row : package.settingsSchema) {
		const auto field = row.toObject();
		const auto key = field[u"key"_q].toString();
		if (!ValidSettingValue(field, (settings.contains(key) ? settings.value(key) : field[u"default"_q]))) {
			error = u"Invalid plugin setting: "_q + key;
			return false;
		}
	}
	return true;
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
	if (!ParseSettingsSchema(manifest.value(u"settingsSchema"_q), parsed.settingsSchema)) {
		error = u"Invalid plugin settings schema."_q;
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
