#include "store.h"

#include "contract.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>

namespace JellyPlugins::Client {

Store::Store(QString root) : _root(QDir::cleanPath(std::move(root))) {
}

QString Store::path(const QString &id) const {
	const auto match = QRegularExpression(QStringLiteral("^[a-z][a-z0-9.-]{2,63}$")).match(id);
	if (!match.hasMatch() || match.capturedLength() != id.size() || QFileInfo(_root).isSymLink()) return {};
	const auto result = _root + QLatin1Char('/') + id + QStringLiteral(".json");
	return QFileInfo(result).isSymLink() ? QString() : result;
}

QStringList Store::ids() const {
	auto result = QStringList();
	for (const auto &name : QDir(_root).entryList({ QStringLiteral("*.json") }, QDir::Files)) {
		const auto id = name.left(name.size() - 5);
		if (!path(id).isEmpty()) result.push_back(id);
	}
	return result;
}

bool Store::exists(const QString &id) const {
	return QFileInfo::exists(_root + QLatin1Char('/') + id + QStringLiteral(".json"));
}

QJsonObject Store::read(const QString &id, QString &error) const {
	const auto filename = path(id);
	auto file = QFile(filename);
	if (filename.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > Limit("packageBytes") * 4) {
		error = QStringLiteral("Cannot read plugin record: ") + id;
		return {};
	}
	const auto document = QJsonDocument::fromJson(file.read(Limit("packageBytes") * 4 + 1));
	if (!document.isObject() || document.object().value(QStringLiteral("schema")) != QJsonValue(1)) {
		error = QStringLiteral("Damaged or unsupported plugin state: ") + id;
		return {};
	}
	error.clear();
	return document.object();
}

bool Store::write(const QString &id, const QJsonObject &record, QString &error) {
	const auto filename = path(id);
	const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact);
	if (filename.isEmpty() || record.value(QStringLiteral("schema")) != QJsonValue(1)
		|| bytes.size() > Limit("packageBytes") * 4 || !QDir().mkpath(_root)) {
		error = QStringLiteral("Invalid plugin record path or size.");
		return false;
	}
	if (QFileInfo::exists(filename)) {
		const auto existing = read(id, error);
		if (existing.isEmpty() || !error.isEmpty()) return false;
	}
	auto file = QSaveFile(filename);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		error = QStringLiteral("Cannot save plugin state atomically: ") + file.errorString();
		return false;
	}
	error.clear();
	return true;
}

bool Store::remove(const QString &id, QString &error) {
	const auto filename = path(id);
	if (filename.isEmpty() || !QFile::remove(filename)) {
		error = QStringLiteral("Cannot remove plugin record: ") + id;
		return false;
	}
	error.clear();
	return true;
}

} // namespace JellyPlugins::Client
