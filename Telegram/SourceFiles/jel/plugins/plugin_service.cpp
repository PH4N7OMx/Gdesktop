#include "jel/plugins/plugin_service.h"

#include "client/launcher.h"
#include "client/scheduler.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_user.h"
#include "jel/plugins/plugin_telegram.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "lang_auto.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QUuid>

#include <algorithm>
#include <climits>
#include <cmath>

namespace JellyPlugins {
namespace {

QString Root(not_null<Main::Session*> session) {
	return cWorkingDir() + u"tdata/jelly-v2/accounts/"_q + QString::number(session->uniqueId());
}

rpl::event_stream<> &GlobalChanges() {
	static auto result = rpl::event_stream<>();
	return result;
}

QJsonArray Times(const std::deque<qint64> &times) {
	auto result = QJsonArray();
	for (const auto time : times) result.push_back(double(time));
	return result;
}

bool ParseTimes(const QJsonValue &value, std::deque<qint64> &result, int maximum) {
	if (!value.isArray() || value.toArray().size() > maximum) return false;
	for (const auto &item : value.toArray()) {
		const auto number = item.toDouble(-1);
		if (!item.isDouble() || !std::isfinite(number) || number < 0 || number > 9007199254740991. || std::floor(number) != number) return false;
		result.push_back(qint64(number));
	}
	std::sort(result.begin(), result.end());
	return true;
}

bool ValidJobs(const QJsonObject &jobs) {
	if (jobs.size() > Client::Limit("schedules")) return false;
	for (auto i = jobs.begin(); i != jobs.end(); ++i) {
		const auto job = i.value().toObject();
		const auto next = job.value(u"nextAt"_q).toDouble(-1);
		auto action = Client::Action();
		auto error = QString();
		if (!i.value().isObject() || job.size() != 2 || !job.value(u"nextAt"_q).isDouble()
			|| !std::isfinite(next) || next < 0 || next > 9007199254740991. || std::floor(next) != next
			|| !Client::PrepareAction(u"schedule.every"_q, { { u"id"_q, i.key() }, { u"seconds"_q, job.value(u"seconds"_q) } }, action, error)) return false;
	}
	return true;
}

} // namespace

struct Service::Entry {
	struct Request {
		Client::Action action;
		QString token;
		qint64 deadline = 0;
		bool waiting = false;
		bool executing = false;
	};
	PluginInfo info;
	QString approvedDigest;
	QString fullRevision;
	QJsonObject storage;
	QJsonObject previous;
	QJsonObject jobs;
	QJsonObject resumeJobs;
	Client::Scheduler scheduler;
	std::unique_ptr<Client::Supervisor> runtime;
	std::map<int, Request> requests;
	std::map<int, QString> events;
	std::deque<qint64> operations;
	quint64 generation = 0;
	int lastRequest = 0;
	int nextEvent = 0;
	bool desired = false;
	bool running = false;
};

Service::Service(not_null<Main::Session*> session)
: _session(session)
, _store(Root(session) + u"/plugins"_q)
, _rateStore(Root(session) + u"/meta"_q)
, _telegram(std::make_unique<TelegramApi>(session)) {
	_clock.start();
	load();
	_timer.setInterval(500);
	QObject::connect(&_timer, &QTimer::timeout, this, [=] { tick(); });
	_timer.start();
	GlobalChanges().events() | rpl::on_next([=] {
		for (const auto &[id, entry] : _entries) if (entry->info.mode == Client::AccessMode::Inherit) revokeExecuting(entry);
		reevaluate();
		_changes.fire({});
	}, _lifetime);
}

Service::~Service() {
	_lifetime.destroy();
	_timer.stop();
	for (const auto &[id, entry] : _entries) stop(entry, false);
}

rpl::producer<> Service::changes() const { return _changes.events(); }
QStringList Service::diagnostics() const { return _diagnostics; }

Service::EntryPtr Service::find(const QString &id) const {
	const auto i = _entries.find(id);
	return i == _entries.end() ? nullptr : i->second;
}

std::vector<PluginInfo> Service::plugins() const {
	auto result = std::vector<PluginInfo>();
	for (const auto &[id, entry] : _entries) {
		auto info = entry->info;
		if (info.mode == Client::AccessMode::Full && entry->fullRevision != Client::Revision()) info.mode = Client::AccessMode::Ask;
		info.approved = entry->approvedDigest == info.package.digest;
		info.canRollback = !entry->previous.isEmpty();
		for (const auto &[requestId, request] : entry->requests) if (request.waiting) ++info.waiting;
		for (const auto &job : entry->scheduler.ids()) {
			const auto next = QDateTime::currentMSecsSinceEpoch() + std::max(qint64(0), entry->scheduler.next(job) - _clock.elapsed());
			if (!info.nextRun || next < info.nextRun) info.nextRun = next;
		}
		result.push_back(std::move(info));
	}
	return result;
}

std::vector<PendingAction> Service::pending() const {
	auto result = std::vector<PendingAction>();
	for (const auto &[id, entry] : _entries) {
		for (const auto &[requestId, request] : entry->requests) {
			if (!request.waiting) continue;
			result.push_back({ request.token, id, entry->info.package.name, entry->info.package.digest,
				_telegram->recipient(request.action), _session->user()->name() + u" ("_q + QString::number(_session->userId().bare) + ')',
				entry->generation, request.action });
		}
	}
	return result;
}

Client::AccessMode Service::globalMode() const {
	const auto value = Core::App().settings().readPref<int>("jellyplugins.v2.mode", int(Client::AccessMode::Remember));
	if (!Client::ValidMode(value, false)) return Client::AccessMode::Remember;
	if (value == int(Client::AccessMode::Full) && Core::App().settings().readPref<QByteArray>("jellyplugins.v2.full-revision") != Client::Revision().toUtf8()) return Client::AccessMode::Remember;
	return Client::AccessMode(value);
}

void Service::setGlobalMode(Client::AccessMode mode) {
	if (!Client::ValidMode(int(mode), false)) return;
	Core::App().settings().writePref<int>("jellyplugins.v2.mode", int(mode));
	Core::App().settings().writePref<QByteArray>("jellyplugins.v2.full-revision", Client::Revision().toUtf8());
	Core::App().saveSettingsDelayed();
	GlobalChanges().fire({});
}

Client::Decision Service::decision(const EntryPtr &entry, const Client::Action &action) const {
	const auto revision = entry->info.mode == Client::AccessMode::Inherit ? Client::Revision() : entry->fullRevision;
	return Client::Evaluate(globalMode(), entry->info.mode, revision, entry->info.rules, action);
}

Service::EntryPtr Service::decode(const QJsonObject &record, QString &error) const {
	auto entry = std::make_shared<Entry>();
	if (!Client::ParsePackage(QJsonDocument(record.value(u"package"_q).toObject()).toJson(QJsonDocument::Compact), entry->info.package, error)) return nullptr;
	error = u"Invalid plugin state: "_q + entry->info.package.id;
	if (!record.value(u"settings"_q).isObject() || !record.value(u"storage"_q).isObject()
		|| !Client::ValidJson(record.value(u"settings"_q), Client::Limit("jsonBytes"))
		|| !Client::ValidJson(record.value(u"storage"_q), Client::Limit("jsonBytes"))
		|| !record.value(u"rules"_q).isArray() || !Client::ValidRules(record.value(u"rules"_q).toArray())
		|| !record.value(u"mode"_q).isDouble() || !Client::ValidMode(record.value(u"mode"_q).toInt(-9), true)
		|| record.value(u"mode"_q).toDouble() != record.value(u"mode"_q).toInt()
		|| !record.value(u"enabled"_q).isBool() || !record.value(u"running"_q).isBool()
		|| !record.value(u"approvedDigest"_q).isString() || !record.value(u"fullRevision"_q).isString()
		|| !record.value(u"jobs"_q).isObject() || !Client::ValidJson(record.value(u"jobs"_q), Client::Limit("jsonBytes"))
		|| !ValidJobs(record.value(u"jobs"_q).toObject())
		|| (record.contains(u"previous"_q) && !record.value(u"previous"_q).isObject())
		|| !ParseTimes(record.value(u"operations"_q), entry->operations, Client::Limit("pluginOperationsPerHour"))) return nullptr;
	entry->info.settings = record.value(u"settings"_q).toObject();
	entry->storage = record.value(u"storage"_q).toObject();
	entry->info.rules = record.value(u"rules"_q).toArray();
	entry->info.mode = Client::AccessMode(record.value(u"mode"_q).toInt());
	entry->approvedDigest = record.value(u"approvedDigest"_q).toString();
	entry->fullRevision = record.value(u"fullRevision"_q).toString();
	entry->desired = record.value(u"enabled"_q).toBool();
	entry->running = record.value(u"running"_q).toBool();
	entry->jobs = record.value(u"jobs"_q).toObject();
	entry->previous = record.value(u"previous"_q).toObject();
	error.clear();
	return entry;
}

QJsonObject Service::encode(const EntryPtr &entry) const {
	return { { u"schema"_q, 1 }, { u"package"_q, entry->info.package.json }, { u"settings"_q, entry->info.settings },
		{ u"storage"_q, entry->storage }, { u"rules"_q, entry->info.rules }, { u"mode"_q, int(entry->info.mode) },
		{ u"approvedDigest"_q, entry->approvedDigest }, { u"fullRevision"_q, entry->fullRevision },
		{ u"enabled"_q, entry->desired }, { u"running"_q, entry->running }, { u"operations"_q, Times(entry->operations) },
		{ u"jobs"_q, entry->jobs }, { u"previous"_q, entry->previous } };
}

bool Service::save(const EntryPtr &entry, QString &error) {
	return _store.write(entry->info.package.id, encode(entry), error);
}

bool Service::saveLimits() {
	auto error = QString();
	const auto saved = _rateStore.write(u"account"_q, { { u"schema"_q, 1 }, { u"operations"_q, Times(_accountOperations) }, { u"blockedUntil"_q, double(_blockedUntil) } }, error);
	if (!saved) _diagnostics.push_back(error);
	_ratesHealthy = saved;
	return saved;
}

void Service::load() {
	auto error = QString();
	if (_rateStore.exists(u"account"_q)) {
		const auto rates = _rateStore.read(u"account"_q, error);
		const auto blocked = rates.value(u"blockedUntil"_q).toDouble(-1);
		_ratesHealthy = error.isEmpty() && ParseTimes(rates.value(u"operations"_q), _accountOperations, Client::Limit("accountOperationsPerHour"))
			&& blocked >= 0 && blocked <= 9007199254740991. && std::floor(blocked) == blocked;
		if (_ratesHealthy) _blockedUntil = qint64(blocked);
		else _diagnostics.push_back(u"Telegram API is blocked because the account rate state is damaged."_q);
	}
	for (const auto &id : _store.ids()) {
		if (_entries.size() >= size_t(Client::Limit("installedPlugins"))) break;
		const auto record = _store.read(id, error);
		if (!error.isEmpty()) { _diagnostics.push_back(error); continue; }
		if (record.value(u"removed"_q).toBool()) continue;
		const auto entry = decode(record, error);
		if (!entry || entry->info.package.id != id) { _diagnostics.push_back(error); continue; }
		_entries.emplace(id, entry);
		if (entry->desired && entry->approvedDigest == entry->info.package.digest) {
			if (entry->running) {
				entry->desired = false;
				entry->running = false;
				entry->info.status = u"Paused after an unexpected shutdown. Enable manually."_q;
				if (!save(entry, error)) _diagnostics.push_back(error);
			} else if (!start(entry, false, error)) log(entry, error);
		}
	}
}

bool Service::install(const QByteArray &data, QString &error) {
	auto package = Client::Package();
	if (!Client::ParsePackage(data, package, error)) return false;
	const auto old = find(package.id);
	if (!old && _entries.size() >= size_t(Client::Limit("installedPlugins"))) { error = u"Too many installed plugins."_q; return false; }
	if (old && old->info.package.digest == package.digest) return true;
	auto entry = std::make_shared<Entry>();
	entry->info.package = std::move(package);
	if (old) {
		entry->info.settings = old->info.settings;
		entry->storage = old->storage;
		entry->operations = old->operations;
		entry->previous = encode(old);
		entry->previous.remove(u"previous"_q);
		entry->previous[u"enabled"_q] = false;
		entry->previous[u"running"_q] = false;
	} else if (_store.exists(entry->info.package.id)) {
		const auto kept = _store.read(entry->info.package.id, error);
		if (!error.isEmpty() || !kept.value(u"removed"_q).toBool()
			|| !kept.value(u"settings"_q).isObject() || !kept.value(u"storage"_q).isObject()
			|| !Client::ValidJson(kept.value(u"settings"_q), Client::Limit("jsonBytes"))
			|| !Client::ValidJson(kept.value(u"storage"_q), Client::Limit("jsonBytes"))) {
			error = u"An unreadable plugin record already exists; it was left unchanged."_q;
			return false;
		}
		entry->info.settings = kept.value(u"settings"_q).toObject();
		entry->storage = kept.value(u"storage"_q).toObject();
	}
	if (!save(entry, error)) return false;
	if (old) stop(old, true, false);
	_entries[entry->info.package.id] = entry;
	_changes.fire({});
	return true;
}

bool Service::start(const EntryPtr &entry, bool manual, QString &error) {
	if (entry->approvedDigest != entry->info.package.digest) { error = u"The current package revision has not been enabled."_q; return false; }
	entry->resumeJobs = manual ? QJsonObject() : entry->jobs;
	entry->jobs = {};
	entry->scheduler.clear();
	entry->lastRequest = 0;
	entry->nextEvent = 0;
	entry->desired = true;
	entry->running = true;
	entry->info.enabled = true;
	const auto generation = ++entry->generation;
	if (!save(entry, error)) { entry->desired = entry->running = entry->info.enabled = false; return false; }
	const auto guard = QPointer<Service>(this);
	const auto weak = std::weak_ptr<Entry>(entry);
	entry->runtime = std::make_unique<Client::Supervisor>(Client::RuntimeCallbacks{
		.state = [=](Client::RuntimeState state, const QString &message) {
			const auto current = weak.lock();
			if (!guard || !current || current->generation != generation) return;
			current->info.state = state;
			if (state == Client::RuntimeState::Failed) { fail(current, message); return; }
			_changes.fire({});
		},
		.frame = [=](const QJsonObject &frame) {
			const auto current = weak.lock();
			if (guard && current && current->generation == generation && current->info.enabled) receive(current, frame);
		},
	});
	if (!entry->runtime->start(Client::HostPath(), QString::number(_session->uniqueId()) + '.' + entry->info.package.id,
		generation, entry->info.package.json, entry->info.settings, error)) {
		fail(entry, error);
		return false;
	}
	entry->info.state = Client::RuntimeState::Starting;
	entry->info.status.clear();
	return true;
}

bool Service::enable(const QString &id, const QString &digest, QString &error) {
	const auto entry = find(id);
	if (!entry || entry->info.package.digest != digest) { error = u"Package changed; reopen its details."_q; return false; }
	if (entry->info.enabled) return true;
	entry->approvedDigest = digest;
	const auto started = start(entry, true, error);
	_changes.fire({});
	return started;
}

void Service::stop(const EntryPtr &entry, bool disable, bool persist) {
	++entry->generation;
	if (disable) { entry->desired = false; entry->jobs = {}; }
	entry->running = false;
	entry->info.enabled = false;
	entry->info.state = Client::RuntimeState::Stopped;
	for (const auto &[id, request] : entry->requests) {
		_telegram->cancel(request.token);
		if (request.executing && request.action.method == u"telegram.sendText"_q) log(entry, u"telegram.sendText: OUTCOME_UNKNOWN; the message may already have been delivered."_q);
	}
	entry->requests.clear();
	entry->events.clear();
	entry->scheduler.clear();
	if (entry->runtime) entry->runtime->stop();
	if (persist) { auto error = QString(); if (!save(entry, error)) log(entry, error); }
}

void Service::fail(const EntryPtr &entry, const QString &error) {
	stop(entry, true);
	entry->info.state = Client::RuntimeState::Failed;
	entry->info.status = error;
	log(entry, error);
	_changes.fire({});
}

bool Service::disable(const QString &id, QString &error) {
	const auto entry = find(id);
	if (!entry) { error = u"Plugin not found."_q; return false; }
	stop(entry, true, false);
	const auto saved = save(entry, error);
	_changes.fire({});
	return saved;
}

void Service::stopAll() {
	for (const auto &[id, entry] : _entries) stop(entry, true);
	_changes.fire({});
}

void Service::log(const EntryPtr &entry, const QString &message) {
	entry->info.log.push_back(QDateTime::currentDateTime().toString(u"HH:mm:ss"_q) + u"  "_q + message.left(Client::Limit("logCharacters")));
	while (entry->info.log.size() > 100) entry->info.log.removeFirst();
}

void Service::receive(const EntryPtr &entry, const QJsonObject &frame) {
	const auto type = frame.value(u"type"_q).toString();
	if (type == u"request"_q) request(entry, frame);
	else if (type == u"log"_q) {
		if (!frame.value(u"message"_q).isString()) { fail(entry, u"Invalid plugin log."_q); return; }
		log(entry, frame.value(u"message"_q).toString());
	} else if (type == u"eventDone"_q) {
		const auto id = frame.value(u"eventId"_q).toInt();
		const auto i = entry->events.find(id);
		if (i == entry->events.end() || frame.value(u"eventId"_q).toDouble() != id) { fail(entry, u"Invalid scheduled-event reply."_q); return; }
		entry->scheduler.complete(i->second, _clock.elapsed());
		entry->events.erase(i);
	}
	_changes.fire({});
}

void Service::request(const EntryPtr &entry, const QJsonObject &frame) {
	const auto id = frame.value(u"requestId"_q).toInt();
	if (id <= entry->lastRequest || id <= 0 || frame.value(u"requestId"_q).toDouble() != id
		|| !frame.value(u"method"_q).isString() || !frame.value(u"params"_q).isObject()
		|| entry->requests.size() >= size_t(Client::Limit("pendingRequests"))) { fail(entry, u"Invalid plugin request or queue overflow."_q); return; }
	entry->lastRequest = id;
	auto action = Client::Action();
	auto error = QString();
	const auto valid = Client::PrepareAction(frame.value(u"method"_q).toString(), frame.value(u"params"_q).toObject(), action, error);
	const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
	entry->requests.emplace(id, Entry::Request{ action, token, _clock.elapsed() + Client::Limit("requestMilliseconds") });
	if (!valid) { reply(entry, id, {}, error); return; }
	const auto policy = decision(entry, action);
	if (policy == Client::Decision::Deny) { reply(entry, id, {}, u"PERMISSION_DENIED"_q); return; }
	if (policy == Client::Decision::Allow) { execute(entry, id); return; }
	auto &pendingRequest = entry->requests[id];
	pendingRequest.waiting = true;
	pendingRequest.deadline = _clock.elapsed() + Client::Limit("approvalMilliseconds");
	entry->runtime->awaitingUser(id, true);
	if (pending().size() == 1) {
		if (const auto controller = _session->tryResolveWindow()) controller->showToast(tr::jel_JellyV2NeedsApproval(tr::now));
	}
}

void Service::reply(const EntryPtr &entry, int id, const QJsonValue &value, const QString &error, int retryAfter) {
	const auto i = entry->requests.find(id);
	if (i == entry->requests.end()) return;
	entry->runtime->awaitingUser(id, false);
	entry->requests.erase(i);
	auto frame = QJsonObject{ { u"type"_q, u"result"_q }, { u"requestId"_q, id } };
	if (error.isEmpty()) frame[u"value"_q] = value;
	else frame[u"error"_q] = QJsonObject{ { u"code"_q, error }, { u"message"_q, error },
		{ u"stage"_q, u"client"_q }, { u"retryAfter"_q, retryAfter } };
	if (!entry->runtime->send(frame)) fail(entry, u"Cannot queue plugin result."_q);
	_changes.fire({});
}

void Service::execute(const EntryPtr &entry, int id) {
	const auto i = entry->requests.find(id);
	if (i == entry->requests.end() || !entry->info.enabled) return;
	const auto action = i->second.action;
	if (decision(entry, action) == Client::Decision::Deny) { reply(entry, id, {}, u"PERMISSION_DENIED"_q); return; }
	i->second.waiting = false;
	entry->runtime->awaitingUser(id, false);
	i->second.deadline = _clock.elapsed() + Client::Limit("requestMilliseconds");
	const auto &params = action.params;
	if (action.method == u"storage.get"_q) {
		reply(entry, id, entry->storage.contains(params.value(u"key"_q).toString()) ? entry->storage.value(params.value(u"key"_q).toString()) : QJsonValue(QJsonValue::Null));
	} else if (action.method == u"storage.set"_q) {
		const auto previous = entry->storage;
		entry->storage[params.value(u"key"_q).toString()] = params.value(u"value"_q);
		auto error = QString();
		if (!Client::ValidJson(entry->storage, Client::Limit("jsonBytes")) || !save(entry, error)) {
			entry->storage = previous;
			reply(entry, id, {}, u"STORAGE_WRITE_FAILED"_q);
		} else reply(entry, id, true);
	} else if (action.method == u"schedule.cancel"_q) {
		const auto job = params.value(u"id"_q).toString();
		entry->scheduler.cancel(job);
		entry->jobs.remove(job);
		auto error = QString();
		if (!save(entry, error)) reply(entry, id, {}, u"STATE_WRITE_FAILED"_q);
		else reply(entry, id, true);
	} else if (action.method == u"schedule.every"_q) {
		const auto job = params.value(u"id"_q).toString();
		const auto seconds = params.value(u"seconds"_q).toInt();
		const auto resumed = entry->resumeJobs.value(job).toObject();
		auto delay = qint64(-1);
		if (resumed.value(u"seconds"_q).toInt() == seconds) {
			const auto remaining = qint64(resumed.value(u"nextAt"_q).toDouble()) - QDateTime::currentMSecsSinceEpoch();
			delay = remaining > 0 ? std::min(remaining, qint64(seconds) * 1000) : qint64(seconds) * 1000;
		}
		if (!entry->scheduler.every(job, seconds, params.value(u"first"_q).toString() == u"immediate"_q, _clock.elapsed(), delay)) {
			reply(entry, id, {}, u"SCHEDULE_LIMIT_OR_DUPLICATE"_q); return;
		}
		entry->jobs[job] = QJsonObject{ { u"seconds"_q, seconds }, { u"nextAt"_q, double(QDateTime::currentMSecsSinceEpoch() + entry->scheduler.next(job) - _clock.elapsed()) } };
		auto error = QString();
		if (!save(entry, error)) { entry->scheduler.cancel(job); entry->jobs.remove(job); reply(entry, id, {}, u"STATE_WRITE_FAILED"_q); }
		else reply(entry, id, true);
	} else if (action.method.startsWith(u"telegram."_q)) {
		const auto now = QDateTime::currentMSecsSinceEpoch();
		while (!_accountOperations.empty() && _accountOperations.front() <= now - 3600000) _accountOperations.pop_front();
		while (!entry->operations.empty() && entry->operations.front() <= now - 3600000) entry->operations.pop_front();
		if (!_ratesHealthy) { reply(entry, id, {}, u"RATE_STATE_UNAVAILABLE"_q); return; }
		if (now < _blockedUntil) { reply(entry, id, {}, u"FLOOD_WAIT"_q, int(std::min(qint64(INT_MAX), (_blockedUntil - now + 999) / 1000))); return; }
		if (_accountOperations.size() >= size_t(Client::Limit("accountOperationsPerHour")) || entry->operations.size() >= size_t(Client::Limit("pluginOperationsPerHour"))) {
			reply(entry, id, {}, u"RATE_LIMIT"_q); return;
		}
		_accountOperations.push_back(now);
		entry->operations.push_back(now);
		auto error = QString();
		if (!saveLimits() || !save(entry, error)) { reply(entry, id, {}, u"RATE_STATE_WRITE_FAILED"_q); return; }
		i->second.executing = true;
		const auto token = i->second.token;
		const auto generation = entry->generation;
		const auto guard = QPointer<Service>(this);
		const auto weak = std::weak_ptr<Entry>(entry);
		_telegram->execute(token, action, [=](TelegramResult result) {
			const auto current = weak.lock();
			if (!guard || !current || current->generation != generation || !current->requests.contains(id)) return;
			if (result.retryAfter > 0) { _blockedUntil = QDateTime::currentMSecsSinceEpoch() + qint64(result.retryAfter) * 1000; if (!saveLimits()) log(current, u"Cannot save FloodWait."_q); }
			log(current, action.method + u": "_q + (result.error.isEmpty() ? u"completed"_q : result.error));
			reply(current, id, result.value, result.error, result.retryAfter);
		});
	} else reply(entry, id, {}, u"METHOD_UNAVAILABLE"_q);
}

bool Service::approve(const QString &token, ApprovalChoice choice, QString &error) {
	for (const auto &[pluginId, entry] : _entries) {
		for (const auto &[id, request] : entry->requests) {
			if (!request.waiting || request.token != token) continue;
			if (request.deadline <= _clock.elapsed() || entry->approvedDigest != entry->info.package.digest) { error = u"This action expired or its package changed."_q; return false; }
			const auto action = request.action;
			const auto remember = choice == ApprovalChoice::RememberExact || choice == ApprovalChoice::RememberChat || choice == ApprovalChoice::DenyAlways;
			if (remember) {
				if (entry->info.rules.size() >= Client::Limit("savedRules")) { error = u"Saved rule limit reached."_q; return false; }
				const auto previousRules = entry->info.rules;
				const auto previousMode = entry->info.mode;
				entry->info.rules.push_back(Client::MakeRule(action, choice != ApprovalChoice::DenyAlways, choice == ApprovalChoice::RememberChat));
				if (choice != ApprovalChoice::DenyAlways && (entry->info.mode == Client::AccessMode::Ask
					|| (entry->info.mode == Client::AccessMode::Inherit && globalMode() == Client::AccessMode::Ask))) entry->info.mode = Client::AccessMode::Remember;
				if (!save(entry, error)) { entry->info.rules = previousRules; entry->info.mode = previousMode; return false; }
			}
			if (choice == ApprovalChoice::DenyOnce || choice == ApprovalChoice::DenyAlways) reply(entry, id, {}, u"PERMISSION_DENIED"_q);
			else execute(entry, id);
			if (remember) reevaluate();
			return true;
		}
	}
	error = u"This action is no longer pending."_q;
	return false;
}

void Service::revokeExecuting(const EntryPtr &entry) {
	auto cancel = std::vector<int>();
	for (const auto &[id, request] : entry->requests) if (request.executing) cancel.push_back(id);
	for (const auto id : cancel) {
		const auto i = entry->requests.find(id);
		if (i == entry->requests.end()) continue;
		const auto request = i->second;
		_telegram->cancel(request.token);
		log(entry, request.action.method + u": "_q + (request.action.method == u"telegram.sendText"_q ? u"OUTCOME_UNKNOWN"_q : u"PERMISSION_REVOKED"_q));
		reply(entry, id, {}, request.action.method == u"telegram.sendText"_q ? u"OUTCOME_UNKNOWN"_q : u"PERMISSION_REVOKED"_q);
	}
}

void Service::reevaluate() {
	for (const auto &[pluginId, entry] : _entries) {
		auto waiting = std::vector<int>();
		for (const auto &[id, request] : entry->requests) if (request.waiting) waiting.push_back(id);
		for (const auto id : waiting) {
			const auto i = entry->requests.find(id);
			if (i == entry->requests.end()) continue;
			const auto policy = decision(entry, i->second.action);
			if (policy == Client::Decision::Allow) execute(entry, id);
			else if (policy == Client::Decision::Deny) reply(entry, id, {}, u"PERMISSION_DENIED"_q);
		}
	}
}

bool Service::setMode(const QString &id, const QString &digest, Client::AccessMode mode, QString &error) {
	const auto entry = find(id);
	if (!entry || entry->info.package.digest != digest || !Client::ValidMode(int(mode), true)) { error = u"Package changed or access mode is invalid."_q; return false; }
	const auto previous = entry->info.mode;
	const auto previousRevision = entry->fullRevision;
	entry->info.mode = mode;
	entry->fullRevision = Client::Revision();
	if (!save(entry, error)) { entry->info.mode = previous; entry->fullRevision = previousRevision; return false; }
	revokeExecuting(entry);
	reevaluate();
	_changes.fire({});
	return true;
}

bool Service::clearRules(const QString &id, const QString &digest, QString &error) {
	const auto entry = find(id);
	if (!entry || entry->info.package.digest != digest) { error = u"Package changed; reopen its details."_q; return false; }
	const auto previous = entry->info.rules;
	entry->info.rules = {};
	if (!save(entry, error)) { entry->info.rules = previous; return false; }
	revokeExecuting(entry);
	reevaluate();
	_changes.fire({});
	return true;
}

bool Service::configure(const QString &id, const QString &digest, const QJsonObject &settings, QString &error) {
	const auto entry = find(id);
	if (!entry || entry->info.package.digest != digest || !Client::ValidJson(settings, Client::Limit("jsonBytes"))) { error = u"Package changed or plugin settings are invalid."_q; return false; }
	const auto previous = entry->info.settings;
	entry->info.settings = settings;
	if (!save(entry, error)) { entry->info.settings = previous; return false; }
	const auto enabled = entry->info.enabled;
	stop(entry, false);
	const auto result = !enabled || start(entry, true, error);
	_changes.fire({});
	return result;
}

bool Service::removeRule(const QString &id, const QString &digest, const QJsonObject &rule, QString &error) {
	const auto entry = find(id);
	if (!entry || entry->info.package.digest != digest || !entry->info.rules.contains(rule)) { error = u"Package or rule changed; reopen its details."_q; return false; }
	const auto previous = entry->info.rules;
	auto remaining = QJsonArray();
	for (const auto &item : previous) if (item != QJsonValue(rule)) remaining.push_back(item);
	entry->info.rules = remaining;
	if (!save(entry, error)) { entry->info.rules = previous; return false; }
	revokeExecuting(entry);
	reevaluate();
	_changes.fire({});
	return true;
}

bool Service::rollback(const QString &id, const QString &digest, QString &error) {
	const auto current = find(id);
	if (!current || current->info.package.digest != digest || current->previous.isEmpty()) { error = u"Package changed or no previous revision is available."_q; return false; }
	const auto restored = decode(current->previous, error);
	if (!restored || restored->info.package.id != id) { error = u"The rollback snapshot is invalid."_q; return false; }
	const auto snapshot = encode(current);
	restored->desired = restored->running = false;
	restored->approvedDigest.clear();
	restored->info.mode = Client::AccessMode::Inherit;
	restored->info.rules = {};
	restored->fullRevision.clear();
	restored->operations = current->operations;
	restored->previous = snapshot;
	restored->previous.remove(u"previous"_q);
	if (!save(restored, error)) return false;
	stop(current, true, false);
	_entries[id] = restored;
	_changes.fire({});
	return true;
}

bool Service::uninstall(const QString &id, const QString &digest, bool removeData, QString &error) {
	const auto entry = find(id);
	if (!entry || entry->info.package.digest != digest) { error = u"Package changed; reopen its details."_q; return false; }
	const auto saved = removeData ? _store.remove(id, error) : _store.write(id, {
		{ u"schema"_q, 1 }, { u"removed"_q, true }, { u"settings"_q, entry->info.settings }, { u"storage"_q, entry->storage },
	}, error);
	if (!saved) return false;
	stop(entry, true, false);
	_entries.erase(id);
	_changes.fire({});
	return true;
}

void Service::tick() {
	for (const auto &[pluginId, entry] : _entries) {
		if (!entry->info.enabled || !entry->runtime) continue;
		auto expired = std::vector<int>();
		for (const auto &[id, request] : entry->requests) if (request.deadline <= _clock.elapsed()) expired.push_back(id);
		for (const auto id : expired) {
			const auto i = entry->requests.find(id);
			if (i == entry->requests.end()) continue;
			const auto request = i->second;
			_telegram->cancel(request.token);
			reply(entry, id, {}, request.executing && request.action.method == u"telegram.sendText"_q ? u"OUTCOME_UNKNOWN"_q : u"REQUEST_EXPIRED"_q);
		}
		if (!entry->info.enabled || entry->runtime->state() != Client::RuntimeState::Ready) continue;
		for (const auto &job : entry->scheduler.due(_clock.elapsed())) {
			if (entry->nextEvent == INT_MAX) { fail(entry, u"Scheduled event limit reached."_q); break; }
			const auto eventId = ++entry->nextEvent;
			entry->events[eventId] = job;
			auto metadata = entry->jobs.value(job).toObject();
			metadata[u"nextAt"_q] = double(QDateTime::currentMSecsSinceEpoch() + entry->scheduler.next(job) - _clock.elapsed());
			entry->jobs[job] = metadata;
			auto error = QString();
			if (!save(entry, error) || !entry->runtime->send({ { u"type"_q, u"event"_q }, { u"eventId"_q, eventId }, { u"event"_q, u"schedule"_q }, { u"scheduleId"_q, job } })) {
				fail(entry, error.isEmpty() ? u"Cannot queue a scheduled event."_q : error);
				break;
			}
		}
	}
}

} // namespace JellyPlugins
