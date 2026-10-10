#pragma once

#include "jel/plugins/plugin_package.h"
#include "rpl/event_stream.h"
#include "rpl/lifetime.h"

#include <QObject>
#include <QTimer>
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace MTP {
class Error;
} // namespace MTP

class HistoryItem;
struct FullMsgId;

namespace JellyPlugins {

struct PluginInfo {
	Package package;
	Permissions granted;
	bool enabled = false;
	bool approved = false;
	QString status;
	QStringList log;
	QJsonObject settings;
	QJsonObject actions;
	QJsonObject messageActions;
};

struct MessageAction {
	QString pluginId;
	QString id;
	QString title;
	QString digest;
	quint64 generation = 0;
};

class Manager final : public QObject {
public:
	explicit Manager(not_null<Main::Session*> session);
	~Manager();
	[[nodiscard]] std::vector<PluginInfo> plugins() const;
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] bool install(const QByteArray &data, QString &error);
	[[nodiscard]] bool enable(const QString &id, const QString &digest, const Permissions &grant, QString &error);
	[[nodiscard]] bool disable(const QString &id, QString &error);
	[[nodiscard]] bool configure(const QString &id, const QJsonObject &settings, QString &error);
	[[nodiscard]] bool uninstall(const QString &id, QString &error);
	void stopAll();
	void runAction(const QString &id, const QString &action);
	[[nodiscard]] std::vector<MessageAction> messageActions(not_null<HistoryItem*> item) const;
	void runMessageAction(const MessageAction &action, FullMsgId message);

private:
	struct Entry;
	using EntryPtr = std::shared_ptr<Entry>;
	void load();
	[[nodiscard]] bool saveState(const EntryPtr &entry, QString &error);
	[[nodiscard]] bool saveAccountLimits();
	[[nodiscard]] bool start(const EntryPtr &entry, QString &error);
	void stop(const EntryPtr &entry);
	void fail(const EntryPtr &entry, const QString &error);
	void log(const EntryPtr &entry, const QString &text);
	void write(const EntryPtr &entry, const QJsonObject &frame);
	void receive(const EntryPtr &entry);
	void request(const EntryPtr &entry, const QJsonObject &frame);
	void reply(const EntryPtr &entry, int id, const QJsonValue &value,
		const QString &error = {}, int retryAfter = 0);
	void sendMessage(const EntryPtr &entry, int id, const QJsonObject &params);
	void telegramAction(const EntryPtr &entry, int id, const QString &method, const QJsonObject &params);
	[[nodiscard]] bool reserveTelegram(const EntryPtr &entry, int id);
	void telegramFailure(const EntryPtr &entry, int id, const MTP::Error &failure);
	void uiAction(const EntryPtr &entry, int id, const QString &method, const QJsonObject &params);
	void fileAction(const EntryPtr &entry, int id, const QString &method, const QJsonObject &params);
	void moneyBalance(const EntryPtr &entry, int id, const QJsonObject &params);
	void openMiniApp(const EntryPtr &entry, int id, const QJsonObject &params);
	[[nodiscard]] bool reserveInteraction(const EntryPtr &entry, int id);
	void getHttp(const EntryPtr &entry, int id, const QJsonObject &params);
	void tick();

	const not_null<Main::Session*> _session;
	QString _root;
	bool _recovering = false;
	std::deque<qint64> _accountSends;
	qint64 _accountBlockedUntil = 0;
	qint64 _interactionBlockedUntil = 0;
	qint64 _notificationBlockedUntil = 0;
	std::map<QString, EntryPtr> _entries;
	QTimer _timer;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

} // namespace JellyPlugins
