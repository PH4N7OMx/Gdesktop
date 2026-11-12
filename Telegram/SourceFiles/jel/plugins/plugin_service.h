#pragma once

#include "client/contract.h"
#include "client/store.h"
#include "client/supervisor.h"
#include "rpl/event_stream.h"
#include "rpl/lifetime.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace JellyPlugins {

class TelegramApi;

struct PluginInfo {
	Client::Package package;
	Client::AccessMode mode = Client::AccessMode::Inherit;
	Client::RuntimeState state = Client::RuntimeState::Stopped;
	QString status;
	QStringList log;
	QJsonObject settings;
	QJsonArray rules;
	bool enabled = false;
	bool approved = false;
	bool canRollback = false;
	int waiting = 0;
	qint64 nextRun = 0;
};

struct PendingAction {
	QString token;
	QString pluginId;
	QString pluginName;
	QString packageDigest;
	QString recipient;
	QString account;
	quint64 generation = 0;
	Client::Action action;
};

enum class ApprovalChoice { Once, RememberExact, RememberChat, DenyOnce, DenyAlways };

class Service final : public QObject {
public:
	explicit Service(not_null<Main::Session*> session);
	~Service();
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] std::vector<PluginInfo> plugins() const;
	[[nodiscard]] std::vector<PendingAction> pending() const;
	[[nodiscard]] QStringList diagnostics() const;
	[[nodiscard]] Client::AccessMode globalMode() const;
	void setGlobalMode(Client::AccessMode mode);
	[[nodiscard]] bool setMode(const QString &id, const QString &digest, Client::AccessMode mode, QString &error);
	[[nodiscard]] bool clearRules(const QString &id, const QString &digest, QString &error);
	[[nodiscard]] bool removeRule(const QString &id, const QString &digest, const QJsonObject &rule, QString &error);
	[[nodiscard]] bool approve(const QString &token, ApprovalChoice choice, QString &error);
	[[nodiscard]] bool install(const QByteArray &data, QString &error);
	[[nodiscard]] bool enable(const QString &id, const QString &digest, QString &error);
	[[nodiscard]] bool disable(const QString &id, QString &error);
	[[nodiscard]] bool configure(const QString &id, const QString &digest, const QJsonObject &settings, QString &error);
	[[nodiscard]] bool rollback(const QString &id, const QString &digest, QString &error);
	[[nodiscard]] bool uninstall(const QString &id, const QString &digest, bool removeData, QString &error);
	void stopAll();

private:
	struct Entry;
	using EntryPtr = std::shared_ptr<Entry>;
	[[nodiscard]] EntryPtr find(const QString &id) const;
	[[nodiscard]] EntryPtr decode(const QJsonObject &record, QString &error) const;
	[[nodiscard]] QJsonObject encode(const EntryPtr &entry) const;
	[[nodiscard]] bool save(const EntryPtr &entry, QString &error);
	[[nodiscard]] bool saveLimits();
	void load();
	[[nodiscard]] bool start(const EntryPtr &entry, bool manual, QString &error);
	void stop(const EntryPtr &entry, bool disable, bool persist = true);
	void fail(const EntryPtr &entry, const QString &error);
	void log(const EntryPtr &entry, const QString &message);
	void receive(const EntryPtr &entry, const QJsonObject &frame);
	void request(const EntryPtr &entry, const QJsonObject &frame);
	void execute(const EntryPtr &entry, int id);
	void reply(const EntryPtr &entry, int id, const QJsonValue &value, const QString &error = {}, int retryAfter = 0);
	void reevaluate();
	void revokeExecuting(const EntryPtr &entry);
	[[nodiscard]] Client::Decision decision(const EntryPtr &entry, const Client::Action &action) const;
	void tick();

	const not_null<Main::Session*> _session;
	Client::Store _store;
	Client::Store _rateStore;
	std::unique_ptr<TelegramApi> _telegram;
	std::map<QString, EntryPtr> _entries;
	std::deque<qint64> _accountOperations;
	qint64 _blockedUntil = 0;
	bool _ratesHealthy = true;
	QStringList _diagnostics;
	QElapsedTimer _clock;
	QTimer _timer;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

} // namespace JellyPlugins
