#include "ayu/plugins/plugin_manager.h"

#include "api/api_common.h"
#include "api/api_sending.h"
#include "ayu/plugins/plugin_sandbox.h"
#include "base/random.h"
#include "core/credits_amount.h"
#include "data/data_user.h"
#include "inline_bots/bot_attach_web_view.h"
#include "window/window_session_controller.h"
#include "lang_auto.h"
#include "data/data_chat_participant_status.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_item_components.h"
#include "history/history_item_reply_markup.h"
#include "main/main_session.h"
#include "apiwrap.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QStringDecoder>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>
#include <climits>
#include <deque>
#include <set>

namespace JellyPlugins {
namespace {

bool SaveJson(const QString &path, const QJsonObject &json) {
	auto file = QSaveFile(path);
	const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
	return file.open(QIODevice::WriteOnly)
		&& file.write(bytes) == bytes.size() && file.commit();
}

QByteArray ReadBounded(const QString &path, int limit) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly) && file.size() <= limit
		? file.read(limit + 1) : QByteArray();
}

QString SerializeChatId(PeerId peer) {
	if (peerIsUser(peer)) {
		return QString::number(peerToUser(peer).bare);
	} else if (peerIsChat(peer)) {
		return QString::number(-qint64(peerToChat(peer).bare));
	} else if (peerIsChannel(peer)) {
		return QString::number(-1000000000000LL - qint64(peerToChannel(peer).bare));
	}
	return {};
}

PeerId PeerFromChatId(const QString &text) {
	auto valid = false;
	const auto value = text.toLongLong(&valid);
	if (!valid || !value) {
		return PeerId();
	}
	if (value > 0) {
		return peerFromUser(UserId(value));
	} else if (value < -1000000000000LL) {
		return peerFromChannel(ChannelId(-value - 1000000000000LL));
	}
	return peerFromChat(ChatId(-value));
}

bool PublicAddress(const QHostAddress &address) {
	if (!address.isGlobal() || address.isLoopback() || address.isLinkLocal()) {
		return false;
	}
	static const auto denied = QStringList{
		u"0.0.0.0/8"_q, u"10.0.0.0/8"_q, u"100.64.0.0/10"_q,
		u"127.0.0.0/8"_q, u"169.254.0.0/16"_q, u"172.16.0.0/12"_q,
		u"192.0.0.0/24"_q, u"192.168.0.0/16"_q, u"198.18.0.0/15"_q,
		u"192.0.2.0/24"_q, u"198.51.100.0/24"_q, u"203.0.113.0/24"_q,
		u"2001::/23"_q, u"2001:db8::/32"_q, u"2002::/16"_q,
	};
	for (const auto &subnet : denied) {
		if (address.isInSubnet(QHostAddress::parseSubnet(subnet))) {
			return false;
		}
	}
	return address.protocol() == QAbstractSocket::IPv4Protocol
		|| address.isInSubnet(QHostAddress::parseSubnet(u"2000::/3"_q));
}

int SentId(const MTPUpdates &updates, uint64 randomId) {
	if (updates.type() == mtpc_updateShortSentMessage) {
		return updates.c_updateShortSentMessage().vid().v;
	}
	const auto find = [&](const auto &list) {
		for (const auto &update : list) {
			if (update.type() == mtpc_updateMessageID
				&& uint64(update.c_updateMessageID().vrandom_id().v) == randomId) {
				return int(update.c_updateMessageID().vid().v);
			}
		}
		return 0;
	};
	if (updates.type() == mtpc_updates) {
		return find(updates.c_updates().vupdates().v);
	} else if (updates.type() == mtpc_updatesCombined) {
		return find(updates.c_updatesCombined().vupdates().v);
	}
	return 0;
}

} // namespace

struct Manager::Entry {
	PluginInfo info;
	QString path;
	QJsonObject storage;
	std::unique_ptr<QProcess> process;
	std::shared_ptr<void> sandbox;
	std::unique_ptr<QNetworkAccessManager> network;
	QByteArray input;
	QPointer<QFileDialog> fileDialog;
	std::map<int, mtpRequestId> telegramRequests;
	std::set<int> pending;
	std::map<int, qint64> pendingDeadlines;
	std::map<int, std::pair<uint64, FullMsgId>> localMessages;
	std::set<int> lookups;
	std::set<QNetworkReply*> replies;
	std::map<QString, std::pair<qint64, int>> timers;
	std::deque<qint64> sends;
	std::deque<qint64> httpRequests;
	qint64 blockedUntil = 0;
	qint64 ackDeadline = 0;
	qint64 frameSecond = 0;
	int frames = 0;
	int awaitingAck = 0;
	int lastRequestId = 0;
	uint64 generation = 0;
};

Manager::Manager(not_null<Main::Session*> session) : _session(session) {
	_root = cWorkingDir() + u"tdata/jelly/plugins/"_q
		+ QString::number(session->uniqueId()) + '/';
	_recovering = QFile::exists(_root + u"active.json"_q);
	const auto limits = QJsonDocument::fromJson(ReadBounded(
		_root + u"limits.json"_q, kStorageLimit)).object();
	_accountBlockedUntil = qint64(limits[u"blockedUntil"_q].toDouble());
	for (const auto &time : limits[u"sends"_q].toArray()) {
		_accountSends.push_back(qint64(time.toDouble()));
	}
	load();
	_session->data().newItemAdded() | rpl::on_next([=](not_null<HistoryItem*> item) {
		if (item->out() || item->isService() || item->id <= 0) {
			return;
		}
		const auto chat = SerializeChatId(item->history()->peer->id);
		if (chat.isEmpty()) {
			return;
		}
		const auto sender = item->from() ? item->from()->id : item->history()->peer->id;
		for (const auto &[id, entry] : _entries) {
			if (entry->info.enabled && entry->info.granted.readChats.contains(chat)) {
				write(entry, {
					{ u"type"_q, u"event"_q }, { u"event"_q, u"message.new"_q },
					{ u"data"_q, QJsonObject{
						{ u"chatId"_q, chat }, { u"messageId"_q, QJsonValue(qint64(item->id.bare)) },
						{ u"senderId"_q, SerializeChatId(sender) },
						{ u"text"_q, item->originalText().text.left(16384) },
						{ u"date"_q, int(item->date()) },
					} },
				});
			}
		}
	}, _lifetime);
	_timer.setInterval(1000);
	QObject::connect(&_timer, &QTimer::timeout, this, [=] { tick(); });
	_timer.start();
}

Manager::~Manager() {
	_timer.stop();
	_lifetime.destroy();
	for (const auto &[id, entry] : _entries) {
		stop(entry);
	}
	QFile::remove(_root + u"active.json"_q);
}

std::vector<PluginInfo> Manager::plugins() const {
	auto result = std::vector<PluginInfo>();
	for (const auto &[id, entry] : _entries) {
		result.push_back(entry->info);
	}
	return result;
}

rpl::producer<> Manager::changes() const {
	return _changes.events();
}

void Manager::load() {
	const auto folders = QDir(_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto &folder : folders) {
		if (_entries.size() >= 16 || QFileInfo(_root + folder).isSymLink()) {
			continue;
		}
		auto entry = std::make_shared<Entry>();
		auto error = QString();
		entry->path = _root + folder + '/';
		if (!ParsePackage(ReadBounded(entry->path + u"package.json"_q, kPackageLimit),
			entry->info.package, error) || entry->info.package.id != folder) {
			continue;
		}
		const auto state = QJsonDocument::fromJson(ReadBounded(
			entry->path + u"state.json"_q, kStorageLimit)).object();
		entry->info.settings = state[u"settings"_q].toObject();
		entry->info.approved = state[u"approvedDigest"_q].toString() == entry->info.package.digest
			&& ParsePermissions(state[u"granted"_q].toObject(), entry->info.granted, error)
			&& IsSubset(entry->info.granted, entry->info.package.permissions);
		entry->storage = QJsonDocument::fromJson(ReadBounded(
			entry->path + u"storage.json"_q, kStorageLimit)).object();
		entry->blockedUntil = qint64(state[u"blockedUntil"_q].toDouble());
		for (const auto &time : state[u"sends"_q].toArray()) {
			entry->sends.push_back(qint64(time.toDouble()));
		}
		_entries.emplace(folder, entry);
		if (state[u"enabled"_q].toBool() && entry->info.approved) {
			if (_recovering) {
				entry->info.status = u"Safe mode after an unexpected shutdown. Enable manually."_q;
				if (!saveState(entry, error)) log(entry, error);
			} else if (!start(entry, error)) {
				entry->info.status = error;
			}
		}
	}
}

bool Manager::saveState(const EntryPtr &entry, QString &error) {
	auto sends = QJsonArray();
	for (const auto time : entry->sends) {
		sends.push_back(double(time));
	}
	if (!SaveJson(entry->path + u"state.json"_q, {
		{ u"enabled"_q, entry->info.enabled },
		{ u"approvedDigest"_q, entry->info.approved ? entry->info.package.digest : QString() },
		{ u"granted"_q, PermissionsJson(entry->info.granted) },
		{ u"settings"_q, entry->info.settings },
		{ u"sends"_q, sends }, { u"blockedUntil"_q, double(entry->blockedUntil) },
	})) {
		error = u"Cannot save plugin state."_q;
		return false;
	}
	return true;
}

bool Manager::saveAccountLimits() {
	auto sends = QJsonArray();
	for (const auto time : _accountSends) sends.push_back(double(time));
	return SaveJson(_root + u"limits.json"_q, {
		{ u"sends"_q, sends }, { u"blockedUntil"_q, double(_accountBlockedUntil) },
	});
}

bool Manager::install(const QByteArray &data, QString &error) {
	auto package = Package();
	if (!ParsePackage(data, package, error)) {
		return false;
	}
	const auto existing = _entries.find(package.id);
	if (existing == _entries.end() && _entries.size() >= 16) {
		error = u"Maximum 16 installed plugins."_q;
		return false;
	}
	auto entry = std::make_shared<Entry>();
	entry->info.package = std::move(package);
	entry->path = _root + entry->info.package.id + '/';
	if (QFileInfo(entry->path).isSymLink() || !QDir().mkpath(entry->path)) {
		error = u"Cannot create plugin directory."_q;
		return false;
	}
	if (existing != _entries.end()) {
		stop(existing->second);
		entry->storage = existing->second->storage;
		entry->info.settings = existing->second->info.settings;
		entry->sends = existing->second->sends;
		entry->blockedUntil = existing->second->blockedUntil;
	}
	if (!SaveJson(entry->path + u"package.json"_q, entry->info.package.json)
		|| !saveState(entry, error)) {
		error = u"Cannot save plugin package or state."_q;
		return false;
	}
	_entries[entry->info.package.id] = entry;
	_changes.fire({});
	return true;
}

bool Manager::enable(const QString &id, const QString &digest, const Permissions &grant, QString &error) {
	const auto found = _entries.find(id);
	if (found != _entries.end() && found->second->info.package.digest != digest) {
		error = u"The plugin changed. Review its current permissions before enabling it."_q;
		return false;
	}
	if (found == _entries.end() || !IsSubset(grant, found->second->info.package.permissions)) {
		error = u"Invalid permission grant."_q;
		return false;
	}
	const auto entry = found->second;
	stop(entry);
	entry->info.granted = grant;
	entry->info.approved = true;
	if (!start(entry, error) || !saveState(entry, error)) {
		stop(entry);
		entry->info.status = error;
		_changes.fire({});
		return false;
	}
	_changes.fire({});
	return true;
}

bool Manager::disable(const QString &id, QString &error) {
	const auto found = _entries.find(id);
	if (found == _entries.end()) {
		return false;
	}
	const auto entry = found->second;
	stop(entry);
	entry->info.approved = false;
	const auto saved = saveState(entry, error);
	_changes.fire({});
	return saved;
}

bool Manager::configure(const QString &id, const QJsonObject &settings, QString &error) {
	const auto found = _entries.find(id);
	if (found == _entries.end() || QJsonDocument(settings).toJson().size() > 8192) {
		error = u"Settings must be a JSON object up to 8 KiB."_q;
		return false;
	}
	const auto entry = found->second;
	stop(entry);
	entry->info.settings = settings;
	const auto saved = saveState(entry, error);
	_changes.fire({});
	return saved;
}

bool Manager::uninstall(const QString &id, QString &error) {
	const auto found = _entries.find(id);
	if (found == _entries.end()) return false;
	const auto entry = found->second;
	stop(entry);
	entry->info.approved = false;
	if (!saveState(entry, error)) return false;
	for (const auto &file : { u"state.json"_q, u"package.json"_q, u"storage.json"_q }) {
		const auto path = entry->path + file;
		if (QFile::exists(path) && !QFile::remove(path)) {
			error = u"Cannot remove plugin data."_q;
			_changes.fire({});
			return false;
		}
	}
	QDir().rmdir(entry->path);
	_entries.erase(found);
	_changes.fire({});
	return true;
}

void Manager::stopAll() {
	for (const auto &[id, entry] : _entries) {
		auto error = QString();
		const auto disabled = disable(id, error);
		if (!disabled) {
			log(entry, error);
		}
	}
}

void Manager::runAction(const QString &id, const QString &action) {
	const auto found = _entries.find(id);
	if (found != _entries.end() && found->second->info.enabled
		&& found->second->info.granted.ui && found->second->info.actions.contains(action)) {
		write(found->second, { { u"type"_q, u"event"_q }, { u"event"_q, u"action"_q },
			{ u"data"_q, QJsonObject{ { u"id"_q, action } } } });
	}
}

bool Manager::start(const EntryPtr &entry, QString &error) {
	if (!SaveJson(_root + u"active.json"_q, { { u"active"_q, true } })) {
		error = u"Cannot save plugin recovery marker."_q;
		return false;
	}
	entry->process = std::make_unique<QProcess>();
	entry->sandbox = PrepareSandbox(*entry->process,
		QString::number(_session->uniqueId()) + '.' + entry->info.package.id, error);
	if (!entry->sandbox) {
		entry->process.reset();
		return false;
	}
	entry->network = std::make_unique<QNetworkAccessManager>();
	entry->network->setProxy(QNetworkProxy::NoProxy);
	const auto weak = std::weak_ptr<Entry>(entry);
	const auto generation = entry->generation;
	QObject::connect(entry->process.get(), &QProcess::readyReadStandardOutput, this, [=] {
		if (const auto current = weak.lock()) {
			receive(current);
		}
	});
	QObject::connect(entry->process.get(), &QProcess::readyReadStandardError, this, [=] {
		if (const auto current = weak.lock()) {
			current->process->readAllStandardError();
		}
	});
	QObject::connect(entry->process.get(), &QProcess::errorOccurred, this, [=](QProcess::ProcessError) {
		QTimer::singleShot(0, this, [=] {
			if (const auto current = weak.lock()) {
				if (current->info.enabled && current->generation == generation)
					fail(current, u"Plugin worker failed."_q);
			}
		});
	});
	QObject::connect(entry->process.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
		this, [=](int code, QProcess::ExitStatus) {
			QTimer::singleShot(0, this, [=] {
				if (const auto current = weak.lock()) {
					if (current->info.enabled && current->generation == generation)
						fail(current, u"Plugin stopped (%1)."_q.arg(code));
				}
			});
		});
	entry->process->start();
	if (!entry->process->waitForStarted(3000)) {
		error = u"Cannot launch isolated worker."_q;
		stop(entry);
		return false;
	}
	entry->info.enabled = true;
	entry->info.status = u"Running"_q;
	write(entry, { { u"type"_q, u"init"_q },
		{ u"package"_q, entry->info.package.json }, { u"settings"_q, entry->info.settings } });
	return true;
}

void Manager::stop(const EntryPtr &entry) {
	entry->info.enabled = false;
	++entry->generation;
	if (entry->fileDialog) {
		entry->fileDialog->disconnect(this);
		entry->fileDialog->close();
		entry->fileDialog->deleteLater();
		entry->fileDialog = nullptr;
	}
	entry->timers.clear();
	entry->info.actions = {};
	for (const auto &[id, requestId] : entry->telegramRequests) {
		_session->api().request(requestId).cancel();
	}
	entry->telegramRequests.clear();
	for (const auto &[id, message] : entry->localMessages) {
		_session->data().unregisterMessageRandomId(message.first);
		_session->data().unregisterMessageSentData(message.first);
		if (const auto local = _session->data().message(message.second)) local->destroy();
	}
	entry->localMessages.clear();
	for (const auto lookup : entry->lookups) {
		QHostInfo::abortHostLookup(lookup);
	}
	entry->lookups.clear();
	for (const auto response : entry->replies) {
		response->disconnect(this);
		response->abort();
	}
	entry->replies.clear();
	if (entry->network) entry->network.release()->deleteLater();
	entry->pending.clear();
	entry->pendingDeadlines.clear();
	entry->input.clear();
	entry->awaitingAck = 0;
	entry->lastRequestId = 0;
	entry->ackDeadline = 0;
	entry->info.status = u"Disabled"_q;
	if (entry->process) {
		entry->process->disconnect(this);
		entry->process->kill();
		entry->process.release()->deleteLater();
	}
	entry->sandbox.reset();
}

void Manager::fail(const EntryPtr &entry, const QString &error) {
	stop(entry);
	entry->info.status = error;
	log(entry, error);
	auto saveError = QString();
	if (!saveState(entry, saveError)) {
		log(entry, saveError);
	}
}

void Manager::log(const EntryPtr &entry, const QString &text) {
	entry->info.log.push_back(QDateTime::currentDateTime().toString(u"HH:mm:ss "_q) + text.left(500));
	while (entry->info.log.size() > 100) {
		entry->info.log.pop_front();
	}
	_changes.fire({});
}

void Manager::write(const EntryPtr &entry, const QJsonObject &frame) {
	if (!entry->info.enabled || !entry->process) {
		return;
	}
	const auto bytes = QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n';
	if (bytes.size() > kFrameLimit || entry->awaitingAck >= 64
		|| entry->process->bytesToWrite() + bytes.size() > kFrameLimit * 2) {
		fail(entry, u"Plugin event queue limit exceeded."_q);
		return;
	}
	if (!entry->awaitingAck++) {
		entry->ackDeadline = QDateTime::currentMSecsSinceEpoch() + 5000;
	}
	if (entry->process->write(bytes) < 0) {
		fail(entry, u"Plugin pipe write failed."_q);
	}
}

void Manager::receive(const EntryPtr &entry) {
	entry->input += entry->process->readAllStandardOutput();
	if (entry->input.size() > kFrameLimit * 2) {
		fail(entry, u"Plugin output limit exceeded."_q);
		return;
	}
	while (entry->process) {
		const auto newline = entry->input.indexOf('\n');
		if (newline < 0) {
			return;
		}
		const auto line = entry->input.left(newline);
		entry->input.remove(0, newline + 1);
		const auto frame = QJsonDocument::fromJson(line).object();
		const auto second = QDateTime::currentSecsSinceEpoch();
		if (entry->frameSecond != second) {
			entry->frameSecond = second;
			entry->frames = 0;
		}
		if (line.size() > kFrameLimit || frame.isEmpty() || ++entry->frames > 100) {
			fail(entry, u"Invalid or excessive plugin output."_q);
			return;
		}
		const auto type = frame[u"type"_q].toString();
		if (type == u"ack"_q && entry->awaitingAck > 0) {
			--entry->awaitingAck;
			entry->ackDeadline = QDateTime::currentMSecsSinceEpoch() + 5000;
		} else if (type == u"log"_q) {
			log(entry, frame[u"text"_q].toString());
		} else if (type == u"fatal"_q) {
			fail(entry, u"Plugin error: "_q + frame[u"error"_q].toString().left(500));
		} else if (type == u"request"_q) {
			request(entry, frame);
		} else {
			fail(entry, u"Invalid plugin protocol."_q);
		}
	}
}

void Manager::reply(const EntryPtr &entry, int id, const QJsonValue &value,
		const QString &error, int retryAfter) {
	if (!entry->pending.erase(id)) {
		return;
	}
	entry->pendingDeadlines.erase(id);
	auto frame = QJsonObject{ { u"type"_q, u"result"_q }, { u"id"_q, id },
		{ u"value"_q, value }, { u"error"_q, error }, { u"retryAfter"_q, retryAfter } };
	if (QJsonDocument(frame).toJson(QJsonDocument::Compact).size() >= kFrameLimit) {
		frame[u"value"_q] = QJsonValue();
		frame[u"error"_q] = u"RESPONSE_LIMIT"_q;
	}
	write(entry, frame);
}

void Manager::request(const EntryPtr &entry, const QJsonObject &frame) {
	const auto id = frame[u"id"_q].toInt();
	if (!entry->info.enabled || id <= entry->lastRequestId || id <= 0
		|| frame[u"id"_q].toDouble() != id || entry->pending.size() >= 32
		|| !frame[u"params"_q].isObject()) {
		fail(entry, u"Invalid plugin request."_q);
		return;
	}
	entry->lastRequestId = id;
	entry->pending.insert(id);
	entry->pendingDeadlines[id] = QDateTime::currentSecsSinceEpoch() + 30;
	const auto method = frame[u"method"_q].toString();
	const auto params = frame[u"params"_q].toObject();
	const auto &grant = entry->info.granted;
	if (method == u"telegram.sendMessage"_q) {
		sendMessage(entry, id, params);
	} else if (method.startsWith(u"telegram."_q)) {
		telegramAction(entry, id, method, params);
	} else if (method == u"files.readText"_q || method == u"files.writeText"_q
		|| method == u"files.readFile"_q || method == u"files.writeFile"_q) {
		fileAction(entry, id, method, params);
	} else if (method == u"money.getBalance"_q) {
		moneyBalance(entry, id, params);
	} else if (method == u"miniApps.open"_q) {
		openMiniApp(entry, id, params);
	} else if (method == u"http.get"_q) {
		getHttp(entry, id, params);
	} else if (method.startsWith(u"storage."_q) && grant.storage) {
		const auto key = params[u"key"_q].toString();
		if (key.isEmpty() || key.size() > 128) {
			reply(entry, id, {}, u"INVALID_KEY"_q);
			return;
		}
		if (method == u"storage.get"_q) {
			reply(entry, id, entry->storage.value(key));
			return;
		}
		auto storage = entry->storage;
		if (method == u"storage.set"_q && params.contains(u"value"_q)) {
			storage[key] = params[u"value"_q];
		} else if (method == u"storage.remove"_q) {
			storage.remove(key);
		} else {
			reply(entry, id, {}, u"INVALID_METHOD"_q);
			return;
		}
		if (QJsonDocument(storage).toJson(QJsonDocument::Compact).size() > kStorageLimit) {
			reply(entry, id, {}, u"STORAGE_LIMIT"_q);
		} else if (!SaveJson(entry->path + u"storage.json"_q, storage)) {
			reply(entry, id, {}, u"STORAGE_WRITE_FAILED"_q);
		} else {
			entry->storage = std::move(storage);
			reply(entry, id, true);
		}
	} else if (method.startsWith(u"timers."_q) && grant.timers) {
		const auto name = params[u"name"_q].toString();
		const auto seconds = params[u"seconds"_q].toInt();
		if (name.isEmpty() || name.size() > 64) {
			reply(entry, id, {}, u"INVALID_TIMER"_q);
		} else if (method == u"timers.cancel"_q) {
			entry->timers.erase(name);
			reply(entry, id, true);
		} else if (method != u"timers.every"_q || seconds < 10 || seconds > 86400
			|| params[u"seconds"_q].toDouble() != seconds
			|| (!entry->timers.contains(name) && entry->timers.size() >= 16)) {
			reply(entry, id, {}, u"INVALID_TIMER"_q);
		} else {
			entry->timers[name] = { QDateTime::currentSecsSinceEpoch() + seconds, seconds };
			reply(entry, id, true);
		}
	} else if (method == u"ui.addAction"_q && grant.ui) {
		const auto action = params[u"id"_q].toString();
		const auto title = params[u"title"_q].toString();
		if (action.isEmpty() || action.size() > 64 || title.isEmpty() || title.size() > 80
			|| (!entry->info.actions.contains(action) && entry->info.actions.size() >= 8)) {
			reply(entry, id, {}, u"INVALID_ACTION"_q);
		} else {
			entry->info.actions[action] = title;
			reply(entry, id, true);
			_changes.fire({});
		}
	} else {
		reply(entry, id, {}, u"PERMISSION_DENIED"_q);
	}
}

void Manager::sendMessage(const EntryPtr &entry, int id, const QJsonObject &params) {
	const auto chat = params[u"chatId"_q].toString();
	const auto text = params[u"text"_q].toString();
	const auto peer = _session->data().peerLoaded(PeerFromChatId(chat));
	if (!entry->info.granted.sendChats.contains(chat)) {
		reply(entry, id, {}, u"PERMISSION_DENIED"_q);
		return;
	}
	if (!peer || !Data::CanSendTexts(peer) || peer->starsPerMessageChecked() != 0
		|| text.isEmpty() || text.size() > 4096 || !params[u"text"_q].isString()) {
		reply(entry, id, {}, u"CHAT_UNAVAILABLE_OR_INVALID_MESSAGE"_q);
		return;
	}
	const auto replyTo = params[u"replyTo"_q].toInt();
	if (replyTo < 0 || (params.contains(u"replyTo"_q)
		&& params[u"replyTo"_q].toDouble() != replyTo)) {
		reply(entry, id, {}, u"INVALID_REPLY"_q);
		return;
	}
	if (!reserveTelegram(entry, id)) return;
	auto action = Api::SendAction(_session->data().history(peer));
	if (replyTo) {
		action.replyTo.messageId = FullMsgId(peer->id, MsgId(replyTo));
	}
	using Flag = MTPmessages_SendMessage::Flag;
	auto flags = MTPmessages_SendMessage::Flags(Flag::f_no_webpage);
	if (replyTo) flags |= Flag::f_reply_to;
	if (params[u"silent"_q].toBool()) flags |= Flag::f_silent;
	const auto randomId = base::RandomValue<uint64>();
	const auto localId = FullMsgId(peer->id, _session->data().nextLocalMessageId());
	auto localFlags = NewMessageFlags(peer);
	Api::FillMessagePostFlags(action, peer, localFlags);
	if (replyTo) localFlags |= MessageFlag::HasReplyInfo;
	const auto localMessage = action.history->addNewLocalMessage({
		.id = localId.msg,
		.flags = localFlags,
		.from = NewMessageFromId(action),
		.replyTo = action.replyTo,
		.date = NewMessageDate(action.options),
		.postAuthor = NewMessagePostAuthor(action),
	}, TextWithEntities{ text }, MTP_messageMediaEmpty());
	_session->data().registerMessageRandomId(randomId, localMessage->fullId());
	_session->data().registerMessageSentData(randomId, peer->id, text);
	entry->localMessages[id] = { randomId, localId };
	const auto generation = entry->generation;
	const auto guard = QPointer<Manager>(this);
	entry->telegramRequests[id] = _session->api().request(MTPmessages_SendMessage(
		MTP_flags(flags), peer->input(), action.mtpReplyTo(), MTP_string(text),
		MTP_long(randomId), MTPReplyMarkup(), MTPVector<MTPMessageEntity>(),
		MTP_int(0), MTP_int(0), MTP_inputPeerEmpty(), MTPInputQuickReplyShortcut(),
		MTP_long(0), MTP_long(0), MTPSuggestedPost(), MTPInputRichMessage()
	)).done([=](const MTPUpdates &updates) {
		if (!guard || !entry->info.enabled || entry->generation != generation) return;
		entry->telegramRequests.erase(id);
		entry->localMessages.erase(id);
		_session->api().applyUpdates(updates, randomId);
		_session->data().unregisterMessageSentData(randomId);
		_session->data().unregisterMessageRandomId(randomId);
		reply(entry, id, QJsonObject{ { u"messageId"_q, SentId(updates, randomId) },
			{ u"accepted"_q, true } });
		log(entry, u"telegram.sendMessage → "_q + chat);
	}).fail([=](const MTP::Error &failure) {
		if (!guard || !entry->info.enabled || entry->generation != generation) return;
		entry->telegramRequests.erase(id);
		entry->localMessages.erase(id);
		_session->data().unregisterMessageRandomId(randomId);
		_session->data().unregisterMessageSentData(randomId);
		if (const auto local = _session->data().message(localId)) local->destroy();
		static const auto flood = QRegularExpression(u"^FLOOD(?:_PREMIUM)?_WAIT_([0-9]+)$"_q);
		const auto match = flood.match(failure.type());
		const auto seconds = match.hasMatch() ? match.captured(1).toInt() : 0;
		if (seconds > 0) {
			entry->blockedUntil = QDateTime::currentSecsSinceEpoch() + seconds;
			_accountBlockedUntil = std::max(_accountBlockedUntil, entry->blockedUntil);
			auto saveError = QString();
			if (!saveState(entry, saveError) || !saveAccountLimits()) log(entry, u"Cannot save FloodWait."_q);
		}
		reply(entry, id, {}, seconds ? u"FLOOD_WAIT"_q : failure.type(), seconds);
	}).handleFloodErrors().send();
}

bool Manager::reserveTelegram(const EntryPtr &entry, int id) {
	const auto now = QDateTime::currentSecsSinceEpoch();
	while (!entry->sends.empty() && entry->sends.front() <= now - 3600) entry->sends.pop_front();
	while (!_accountSends.empty() && _accountSends.front() <= now - 3600) _accountSends.pop_front();
	const auto blocked = std::max(entry->blockedUntil, _accountBlockedUntil);
	if (blocked > now) {
		reply(entry, id, {}, u"FLOOD_WAIT"_q, int(blocked - now));
		return false;
	}
	if (_accountSends.size() >= 60
		|| entry->sends.size() >= size_t(entry->info.granted.maxMessagesPerHour)
		|| (!entry->sends.empty() && entry->sends.back() > now - 3)) {
		reply(entry, id, {}, u"RATE_LIMIT"_q,
			_accountSends.size() >= 60 ? int(_accountSends.front() + 3600 - now)
				: entry->sends.size() >= size_t(entry->info.granted.maxMessagesPerHour)
				? int(entry->sends.front() + 3600 - now) : 3);
		return false;
	}
	entry->sends.push_back(now);
	_accountSends.push_back(now);
	auto error = QString();
	if (!saveState(entry, error) || !saveAccountLimits()) {
		reply(entry, id, {}, u"STATE_WRITE_FAILED"_q);
		return false;
	}
	return true;
}

void Manager::telegramFailure(const EntryPtr &entry, int id, const MTP::Error &failure) {
	entry->telegramRequests.erase(id);
	static const auto flood = QRegularExpression(u"^FLOOD(?:_PREMIUM)?_WAIT_([0-9]+)$"_q);
	const auto match = flood.match(failure.type());
	const auto seconds = match.hasMatch() ? match.captured(1).toInt() : 0;
	if (seconds > 0) {
		entry->blockedUntil = QDateTime::currentSecsSinceEpoch() + seconds;
		_accountBlockedUntil = std::max(_accountBlockedUntil, entry->blockedUntil);
		auto error = QString();
		if (!saveState(entry, error) || !saveAccountLimits()) log(entry, u"Cannot save FloodWait."_q);
	}
	reply(entry, id, {}, seconds ? u"FLOOD_WAIT"_q : failure.type(), seconds);
}

void Manager::telegramAction(const EntryPtr &entry, int id,
		const QString &method, const QJsonObject &params) {
	const auto &grant = entry->info.granted;
	const auto chat = params[u"chatId"_q].toString();
	const auto scope = [&]() -> const QStringList* {
		if (method == u"telegram.joinChannel"_q) return &grant.joinChannels;
		if (method == u"telegram.clickButton"_q) return &grant.botChats;
		if (method == u"telegram.sendAttachment"_q) return &grant.sendChats;
		if (method == u"telegram.editMessage"_q) return &grant.editChats;
		if (method == u"telegram.setReaction"_q) return &grant.reactionChats;
		if (method == u"telegram.getHistory"_q) return &grant.historyChats;
		return nullptr;
	}();
	if (!scope || !scope->contains(chat)) {
		reply(entry, id, {}, u"PERMISSION_DENIED"_q);
		return;
	}
	const auto peer = _session->data().peerLoaded(PeerFromChatId(chat));
	if (!peer) {
		reply(entry, id, {}, u"CHAT_UNAVAILABLE"_q);
		return;
	}
	const auto integer = [&](const QString &key, int minimum, int maximum, int fallback) {
		if (params.contains(key) && !params[key].isDouble()) return -1;
		const auto value = params.value(key).toDouble(fallback);
		return value >= minimum && value <= maximum && value == int(value)
			? int(value) : -1;
	};
	const auto messageId = integer(u"messageId"_q, 1, INT_MAX, -1);
	const auto item = messageId > 0
		? _session->data().message(FullMsgId(peer->id, MsgId(messageId))) : nullptr;
	const auto generation = entry->generation;
	const auto guard = QPointer<Manager>(this);
	const auto active = [=] {
		return guard && entry->info.enabled && entry->generation == generation;
	};
	const auto failed = [=](const MTP::Error &failure) {
		if (active()) telegramFailure(entry, id, failure);
	};
	const auto done = [=](const MTPUpdates &updates) {
		if (!active()) return;
		entry->telegramRequests.erase(id);
		_session->api().applyUpdates(updates);
		reply(entry, id, true);
		log(entry, method + u" → "_q + chat);
	};
	if (method == u"telegram.joinChannel"_q) {
		const auto channel = peer->asChannel();
		if (!channel) {
			reply(entry, id, {}, u"INVALID_CHANNEL"_q);
			return;
		}
		if (!reserveTelegram(entry, id)) return;
		entry->telegramRequests[id] = _session->api().request(MTPchannels_JoinChannel(
			channel->inputChannel()
		)).done([=](const MTPmessages_ChatInviteJoinResult &result) {
			if (!active()) return;
			entry->telegramRequests.erase(id);
			if (result.type() == mtpc_messages_chatInviteJoinResultOk) {
				done(result.c_messages_chatInviteJoinResultOk().vupdates());
			} else {
				reply(entry, id, {}, u"INTERACTIVE_JOIN_REQUIRED"_q);
			}
		}).fail(failed).handleFloodErrors().send();
	} else if (method == u"telegram.getHistory"_q) {
		const auto before = integer(u"beforeId"_q, 0, INT_MAX, 0);
		const auto limit = integer(u"limit"_q, 1, 100, 20);
		if (before < 0 || limit < 0) {
			reply(entry, id, {}, u"INVALID_HISTORY_OPTIONS"_q);
			return;
		}
		if (!reserveTelegram(entry, id)) return;
		entry->telegramRequests[id] = _session->api().request(MTPmessages_GetHistory(
			peer->input(), MTP_int(before), MTP_int(0), MTP_int(0),
			MTP_int(limit), MTP_int(0), MTP_int(0), MTP_long(0)
		)).done([=](const MTPmessages_Messages &result) {
			if (!active()) return;
			entry->telegramRequests.erase(id);
			auto messages = QJsonArray();
			const auto collect = [&](const auto &data) {
				_session->data().processUsers(data.vusers());
				_session->data().processChats(data.vchats());
				for (const auto &message : data.vmessages().v) {
					if (messages.size() >= limit) break;
					const auto loaded = _session->data().addNewMessage(
						message, MessageFlags(), NewMessageType::Existing);
					if (!loaded || loaded->isService() || loaded->history()->peer != peer) continue;
					auto buttons = QJsonArray();
					if (const auto markup = loaded->inlineReplyMarkup()) {
						for (auto row = 0; row < int(markup->data.rows.size()); ++row) {
							const auto &columns = markup->data.rows[row];
							for (auto column = 0; column < int(columns.size()); ++column) {
								const auto &button = columns[column];
								if (button.type == HistoryMessageMarkupButton::Type::Callback) {
									buttons.push_back(QJsonObject{
										{ u"row"_q, row }, { u"column"_q, column },
										{ u"text"_q, button.text.left(80) },
									});
								}
							}
						}
					}
					messages.push_back(QJsonObject{
						{ u"chatId"_q, chat }, { u"messageId"_q, int(loaded->id.bare) },
						{ u"senderId"_q, loaded->from() ? SerializeChatId(loaded->from()->id) : chat },
						{ u"text"_q, loaded->originalText().text.left(16384) },
						{ u"date"_q, int(loaded->date()) },
						{ u"hasAttachment"_q, loaded->media()
							&& (loaded->media()->photo() || loaded->media()->document()) },
						{ u"buttons"_q, buttons },
					});
				}
			};
			result.match(
				[&](const MTPDmessages_messages &data) { collect(data); },
				[&](const MTPDmessages_messagesSlice &data) { collect(data); },
				[&](const MTPDmessages_channelMessages &data) { collect(data); },
				[&](const MTPDmessages_messagesNotModified &) {});
			reply(entry, id, messages);
			log(entry, method + u" → "_q + chat);
		}).fail(failed).handleFloodErrors().send();
	} else if (method == u"telegram.sendAttachment"_q) {
		const auto sourceChat = params[u"sourceChatId"_q].toString();
		if (!grant.attachmentChats.contains(sourceChat)) {
			reply(entry, id, {}, u"PERMISSION_DENIED"_q);
			return;
		}
		const auto source = _session->data().peerLoaded(PeerFromChatId(sourceChat));
		const auto attachment = source && messageId > 0
			? _session->data().message(FullMsgId(source->id, MsgId(messageId))) : nullptr;
		if (!attachment || !attachment->allowsForward() || !attachment->media()
			|| (!attachment->media()->photo() && !attachment->media()->document())
			|| attachment->media()->webpage() || attachment->media()->invoice()
			|| !Data::CanSendTexts(peer) || peer->starsPerMessageChecked() != 0) {
			reply(entry, id, {}, u"ATTACHMENT_UNAVAILABLE"_q);
			return;
		}
		if (!reserveTelegram(entry, id)) return;
		using Flag = MTPmessages_ForwardMessages::Flag;
		auto flags = MTPmessages_ForwardMessages::Flags(Flag::f_drop_author);
		if (params[u"silent"_q].toBool()) flags |= Flag::f_silent;
		const auto randomId = base::RandomValue<uint64>();
		entry->telegramRequests[id] = _session->api().request(MTPmessages_ForwardMessages(
			MTP_flags(flags), source->input(), MTP_vector<MTPint>({ MTP_int(messageId) }),
			MTP_vector<MTPlong>({ MTP_long(randomId) }), peer->input(),
			MTP_int(0), MTPInputReplyTo(), MTP_int(0), MTP_int(0), MTP_inputPeerEmpty(),
			MTPInputQuickReplyShortcut(), MTP_long(0), MTP_int(0), MTP_long(0), MTPSuggestedPost()
		)).done([=](const MTPUpdates &updates) {
			if (!active()) return;
			entry->telegramRequests.erase(id);
			_session->api().applyUpdates(updates);
			reply(entry, id, QJsonObject{ { u"accepted"_q, true },
				{ u"messageId"_q, SentId(updates, randomId) } });
			log(entry, method + u" → "_q + chat);
		}).fail(failed).handleFloodErrors().send();
	} else if (messageId <= 0) {
		reply(entry, id, {}, u"INVALID_MESSAGE_ID"_q);
	} else if (method == u"telegram.clickButton"_q) {
		const auto row = integer(u"row"_q, 0, 100, -1);
		const auto column = integer(u"column"_q, 0, 100, -1);
		const auto button = item && row >= 0 && column >= 0
			? HistoryMessageMarkupButton::Get(&_session->data(), item->fullId(), row, column) : nullptr;
		if (!button || button->type != HistoryMessageMarkupButton::Type::Callback
			|| button->requestId || !item->getMessageBot()) {
			reply(entry, id, {}, u"BUTTON_UNAVAILABLE"_q);
			return;
		}
		if (!reserveTelegram(entry, id)) return;
		entry->telegramRequests[id] = _session->api().request(MTPmessages_GetBotCallbackAnswer(
			MTP_flags(MTPmessages_GetBotCallbackAnswer::Flag::f_data),
			peer->input(), MTP_int(messageId), MTP_bytes(button->data), MTP_inputCheckPasswordEmpty()
		)).done([=](const MTPmessages_BotCallbackAnswer &answer) {
			if (!active()) return;
			entry->telegramRequests.erase(id);
			const auto &data = answer.data();
			reply(entry, id, QJsonObject{
				{ u"text"_q, data.vmessage() ? qs(*data.vmessage()).left(4096) : QString() },
				{ u"alert"_q, data.is_alert() },
			});
			log(entry, method + u" → "_q + chat);
		}).fail(failed).handleFloodErrors().send();
	} else if (method == u"telegram.editMessage"_q) {
		const auto text = params[u"text"_q].toString();
		if (!item || !item->out() || !item->allowsEdit(TimeId(QDateTime::currentSecsSinceEpoch()))
			|| text.isEmpty() || text.size() > 4096) {
			reply(entry, id, {}, u"MESSAGE_NOT_EDITABLE"_q);
			return;
		}
		if (!reserveTelegram(entry, id)) return;
		entry->telegramRequests[id] = _session->api().request(MTPmessages_EditMessage(
			MTP_flags(MTPmessages_EditMessage::Flag::f_message | MTPmessages_EditMessage::Flag::f_no_webpage),
			peer->input(), MTP_int(messageId), MTP_string(text), MTPInputMedia(),
			MTPReplyMarkup(), MTPVector<MTPMessageEntity>(), MTP_int(0), MTP_int(0),
			MTP_int(0), MTPInputRichMessage()
		)).done(done).fail(failed).handleFloodErrors().send();
	} else if (method == u"telegram.setReaction"_q) {
		const auto emoji = params[u"emoji"_q].toString();
		if (!params[u"emoji"_q].isString() || emoji.size() > 32) {
			reply(entry, id, {}, u"INVALID_REACTION"_q);
			return;
		}
		if (!reserveTelegram(entry, id)) return;
		auto reactions = QVector<MTPReaction>();
		if (!emoji.isEmpty()) reactions.push_back(MTP_reactionEmoji(MTP_string(emoji)));
		entry->telegramRequests[id] = _session->api().request(MTPmessages_SendReaction(
			MTP_flags(MTPmessages_SendReaction::Flag::f_reaction),
			peer->input(), MTP_int(messageId), MTP_vector<MTPReaction>(reactions)
		)).done(done).fail(failed).handleFloodErrors().send();
	}
}

bool Manager::reserveInteraction(const EntryPtr &entry, int id) {
	const auto now = QDateTime::currentSecsSinceEpoch();
	for (const auto &[key, other] : _entries) {
		if (other->fileDialog) {
			reply(entry, id, {}, u"INTERACTION_BUSY"_q);
			return false;
		}
	}
	if (now < _interactionBlockedUntil) {
		reply(entry, id, {}, u"RATE_LIMIT"_q, int(_interactionBlockedUntil - now));
		return false;
	}
	_interactionBlockedUntil = now + 30;
	return true;
}

void Manager::fileAction(const EntryPtr &entry, int id, const QString &method, const QJsonObject &params) {
	const auto binary = (method == u"files.readFile"_q || method == u"files.writeFile"_q);
	const auto writing = (method == u"files.writeText"_q || method == u"files.writeFile"_q);
	if (!(writing ? entry->info.granted.fileWrite : entry->info.granted.fileRead)) {
		reply(entry, id, {}, u"PERMISSION_DENIED"_q);
		return;
	}
	constexpr auto kTextLimit = 1024 * 1024;
	const auto contentKey = binary ? u"base64"_q : u"text"_q;
	const auto content = params[contentKey].toString();
	const auto bytes = !writing ? QByteArray() : binary
		? QByteArray::fromBase64(content.toLatin1()) : content.toUtf8();
	const auto name = params[u"suggestedName"_q].toString(binary ? u"export.bin"_q : u"export.txt"_q);
	static const auto safeName = QRegularExpression(u"^[^/\\\\:*?\"<>|\\x00-\\x1f]{1,120}$"_q);
	if (writing && (!params[contentKey].isString() || bytes.size() > kTextLimit
		|| (binary && QString::fromLatin1(bytes.toBase64()) != content)
		|| (params.contains(u"suggestedName"_q) && !params[u"suggestedName"_q].isString())
		|| name.isEmpty() || (safeName.match(name).capturedLength() != name.size()) || name == u"."_q || name == u".."_q)) {
		reply(entry, id, {}, u"INVALID_FILE_CONTENT_OR_NAME"_q);
		return;
	}
	if (!reserveInteraction(entry, id)) return;
	const auto title = (writing ? tr::ayu_JellyFileWrite(tr::now) : tr::ayu_JellyFileRead(tr::now))
		+ u" — "_q + entry->info.package.name;
	const auto dialog = new QFileDialog(nullptr, title);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setAcceptMode(writing ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
	dialog->setFileMode(writing ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
	if (writing) dialog->selectFile(name);
	entry->fileDialog = dialog;
	// Choosing a file is a user interaction, not a timed plugin operation.
	entry->pendingDeadlines.erase(id);
	const auto generation = entry->generation;
	connect(dialog, &QDialog::finished, this, [=](int result) {
		entry->fileDialog = nullptr;
		if (!entry->info.enabled || generation != entry->generation) return;
		const auto paths = dialog->selectedFiles();
		if (result != QDialog::Accepted || paths.size() != 1) {
			reply(entry, id, {}, u"USER_CANCELLED"_q);
			return;
		}
		const auto info = QFileInfo(paths.front());
		const auto parent = QFileInfo(info.absolutePath()).canonicalFilePath();
		const auto path = info.exists() ? info.canonicalFilePath() : parent + '/' + info.fileName();
		const auto privateRoot = QFileInfo(cWorkingDir() + u"tdata"_q).canonicalFilePath();
		if (parent.isEmpty() || path.isEmpty() || info.isSymLink()
			|| (!privateRoot.isEmpty() && (path.compare(privateRoot, Qt::CaseInsensitive) == 0
				|| path.startsWith(privateRoot + '/', Qt::CaseInsensitive)))) {
			reply(entry, id, {}, u"FILE_UNAVAILABLE"_q);
			return;
		}
		if (writing) {
			auto file = QSaveFile(path);
			if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
				reply(entry, id, {}, u"FILE_WRITE_FAILED"_q);
				return;
			}
			reply(entry, id, QJsonObject{ { u"name"_q, info.fileName() } });
		} else {
			auto file = QFile(path);
			if (!info.isFile() || !file.open(QIODevice::ReadOnly) || file.size() > kTextLimit) {
				reply(entry, id, {}, u"FILE_UNAVAILABLE_OR_TOO_LARGE"_q);
				return;
			}
			const auto content = file.read(kTextLimit + 1);
			if (content.size() > kTextLimit || file.error() != QFileDevice::NoError) {
				reply(entry, id, {}, u"FILE_UNAVAILABLE_OR_TOO_LARGE"_q);
				return;
			}
			if (binary) {
				reply(entry, id, QJsonObject{
					{ u"name"_q, info.fileName() },
					{ u"base64"_q, QString::fromLatin1(content.toBase64()) },
				});
				return;
			}
			auto decoder = QStringDecoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
			const auto text = QString(decoder(content));
			if (decoder.hasError()) {
				reply(entry, id, {}, u"FILE_NOT_UTF8_OR_TOO_LARGE"_q);
				return;
			}
			reply(entry, id, QJsonObject{ { u"name"_q, info.fileName() }, { u"text"_q, text } });
		}
	});
	dialog->show();
}

void Manager::moneyBalance(const EntryPtr &entry, int id, const QJsonObject &params) {
	if (!entry->info.granted.moneyRead) {
		reply(entry, id, {}, u"PERMISSION_DENIED"_q);
		return;
	}
	const auto currency = params[u"currency"_q].toString(u"stars"_q);
	if ((params.contains(u"currency"_q) && !params[u"currency"_q].isString())
		|| (currency != u"stars"_q && currency != u"ton"_q)) {
		reply(entry, id, {}, u"INVALID_CURRENCY"_q);
		return;
	}
	if (!reserveTelegram(entry, id)) return;
	const auto generation = entry->generation;
	const auto guard = QPointer<Manager>(this);
	using Flag = MTPpayments_GetStarsStatus::Flag;
	entry->telegramRequests[id] = _session->api().request(MTPpayments_GetStarsStatus(
		MTP_flags(currency == u"ton"_q
			? MTPpayments_GetStarsStatus::Flags(Flag::f_ton)
			: MTPpayments_GetStarsStatus::Flags()),
		MTP_inputPeerSelf()
	)).done([=](const MTPpayments_StarsStatus &result) {
		if (!guard || !entry->info.enabled || generation != entry->generation) return;
		entry->telegramRequests.erase(id);
		const auto amount = CreditsAmountFromTL(result.data().vbalance());
		reply(entry, id, QJsonObject{
			{ u"currency"_q, currency }, { u"whole"_q, QString::number(amount.whole()) },
			{ u"nanos"_q, QJsonValue(qint64(amount.nano())) },
		});
	}).fail([=](const MTP::Error &error) {
		if (!guard || !entry->info.enabled || generation != entry->generation) return;
		telegramFailure(entry, id, error);
	}).send();
}

void Manager::openMiniApp(const EntryPtr &entry, int id, const QJsonObject &params) {
	const auto botId = params[u"botId"_q].toString();
	if (!entry->info.granted.webviewBots.contains(botId)) {
		reply(entry, id, {}, u"PERMISSION_DENIED"_q);
		return;
	}
	const auto peer = PeerFromChatId(botId);
	const auto bot = peerIsUser(peer) ? _session->data().userLoaded(peerToUser(peer)) : nullptr;
	const auto start = params[u"startParam"_q].toString();
	static const auto token = QRegularExpression(u"^[A-Za-z0-9_-]{0,512}$"_q);
	const auto controller = _session->tryResolveWindow();
	if (!bot || !bot->botInfo || !bot->botInfo->hasMainApp || !controller
		|| (params.contains(u"startParam"_q) && !params[u"startParam"_q].isString())
		|| (!token.match(start).hasMatch() || token.match(start).capturedLength() != start.size())) {
		reply(entry, id, {}, u"MINI_APP_UNAVAILABLE_OR_INVALID_PARAMETER"_q);
		return;
	}
	if (!reserveInteraction(entry, id) || !reserveTelegram(entry, id)) return;
	_session->attachWebView().open({
		.bot = bot,
		.context = { .controller = controller, .maySkipConfirmation = false },
		.button = { .startCommand = start },
		.source = InlineBots::WebViewSourceLinkBotProfile{ .token = start },
	});
	// This acknowledges the UI request, not the app's loading or payment result.
	reply(entry, id, QJsonObject{ { u"requested"_q, true } });
}

void Manager::getHttp(const EntryPtr &entry, int id, const QJsonObject &params) {
	const auto original = QUrl(params[u"url"_q].toString(), QUrl::StrictMode);
	const auto host = original.host().toLower();
	if (!original.isValid() || original.scheme() != u"https"_q
		|| original.port(443) != 443 || !original.userInfo().isEmpty()
		|| !entry->info.granted.httpHosts.contains(host) || !original.fragment().isEmpty()) {
		reply(entry, id, {}, u"PERMISSION_DENIED"_q);
		return;
	}
	const auto now = QDateTime::currentSecsSinceEpoch();
	if (entry->lookups.size() >= 8 || entry->replies.size() >= 8) {
		reply(entry, id, {}, u"HTTP_RATE_LIMIT"_q, 15);
		return;
	}
	while (!entry->httpRequests.empty() && entry->httpRequests.front() <= now - 60) {
		entry->httpRequests.pop_front();
	}
	if (entry->httpRequests.size() >= 20) {
		reply(entry, id, {}, u"HTTP_RATE_LIMIT"_q, 60);
		return;
	}
	entry->httpRequests.push_back(now);
	const auto generation = entry->generation;
	const auto lookup = std::make_shared<int>(0);
	*lookup = QHostInfo::lookupHost(host, this, [=](const QHostInfo &resolved) {
		entry->lookups.erase(*lookup);
		if (!entry->info.enabled || entry->generation != generation || !entry->network) return;
		if (resolved.error() != QHostInfo::NoError || resolved.addresses().isEmpty()) {
			reply(entry, id, {}, u"DNS_FAILED"_q);
			return;
		}
		for (const auto &address : resolved.addresses()) {
			if (!PublicAddress(address)) {
				reply(entry, id, {}, u"PRIVATE_NETWORK_BLOCKED"_q);
				return;
			}
		}
		auto pinned = original;
		pinned.setHost(resolved.addresses().front().toString());
		auto request = QNetworkRequest(pinned);
		request.setPeerVerifyName(host);
		request.setRawHeader("Host", host.toUtf8());
		request.setRawHeader("User-Agent", "JellyPlugins/1");
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
		request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
		request.setTransferTimeout(15000);
		const auto response = entry->network->get(request);
		entry->replies.insert(response);
		response->setReadBufferSize(kPackageLimit + 1);
		const auto body = std::make_shared<QByteArray>();
		QObject::connect(response, &QNetworkReply::readyRead, response, [=] {
			*body += response->read(kPackageLimit + 1 - body->size());
			if (body->size() > kPackageLimit) response->abort();
		});
		QObject::connect(response, &QNetworkReply::finished, this, [=] {
			entry->replies.erase(response);
			if (!entry->info.enabled || entry->generation != generation) {
				response->deleteLater();
				return;
			}
			*body += response->read(kPackageLimit + 1 - body->size());
			const auto status = response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (body->size() > kPackageLimit) {
				reply(entry, id, {}, u"HTTP_BODY_LIMIT"_q);
			} else if (status >= 300 && status < 400) {
				reply(entry, id, {}, u"HTTP_REDIRECT_BLOCKED"_q);
			} else if (response->error() != QNetworkReply::NoError) {
				reply(entry, id, {}, u"HTTP_FAILED"_q);
			} else {
				reply(entry, id, QJsonObject{ { u"status"_q, status },
					{ u"text"_q, QString::fromUtf8(*body) } });
				log(entry, u"http.get → "_q + host);
			}
			response->deleteLater();
		});
	});
	entry->lookups.insert(*lookup);
}

void Manager::tick() {
	const auto now = QDateTime::currentSecsSinceEpoch();
	for (const auto &[id, entry] : _entries) {
		if (!entry->info.enabled) continue;
		const auto overdue = std::any_of(entry->pendingDeadlines.begin(), entry->pendingDeadlines.end(),
			[&](const auto &request) { return request.second <= now; });
		if (overdue) {
			fail(entry, u"Plugin operation timed out; outcome may be unknown."_q);
			continue;
		}
		if (entry->awaitingAck && entry->ackDeadline < QDateTime::currentMSecsSinceEpoch()) {
			fail(entry, u"Plugin execution timed out."_q);
			continue;
		}
		auto due = QStringList();
		for (auto &[name, timer] : entry->timers) {
			if (timer.first <= now) {
				timer.first = now + timer.second;
				due.push_back(name);
			}
		}
		for (const auto &name : due) {
			write(entry, { { u"type"_q, u"event"_q }, { u"event"_q, u"timer"_q },
				{ u"data"_q, QJsonObject{ { u"name"_q, name }, { u"timestamp"_q, double(now) } } } });
		}
	}
}

} // namespace JellyPlugins
