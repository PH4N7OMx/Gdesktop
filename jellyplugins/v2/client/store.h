#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QStringList>

namespace JellyPlugins::Client {

class Store final {
public:
	explicit Store(QString root);
	[[nodiscard]] QStringList ids() const;
	[[nodiscard]] bool exists(const QString &id) const;
	[[nodiscard]] QJsonObject read(const QString &id, QString &error) const;
	[[nodiscard]] bool write(const QString &id, const QJsonObject &record, QString &error);
	[[nodiscard]] bool remove(const QString &id, QString &error);

private:
	[[nodiscard]] QString path(const QString &id) const;
	QString _root;

};

} // namespace JellyPlugins::Client
