#include "scheduler.h"

#include "contract.h"

namespace JellyPlugins::Client {

bool Scheduler::every(const QString &id, int seconds, bool immediate, qint64 now, qint64 delay) {
	if (seconds < 60 || seconds > 604800 || _jobs.count(id) || _jobs.size() >= size_t(Limit("schedules"))) return false;
	const auto interval = qint64(seconds) * 1000;
	_jobs.emplace(id, Job{ interval, now + (delay >= 0 ? delay : immediate ? 0 : interval), false });
	return true;
}

void Scheduler::cancel(const QString &id) { _jobs.erase(id); }
void Scheduler::clear() { _jobs.clear(); }

QStringList Scheduler::due(qint64 now) {
	auto result = QStringList();
	for (auto &[id, job] : _jobs) {
		if (job.busy || job.next > now) continue;
		job.busy = true;
		job.next = now + job.interval;
		result.push_back(id);
	}
	return result;
}

void Scheduler::complete(const QString &id, qint64 now) {
	const auto i = _jobs.find(id);
	if (i == _jobs.end()) return;
	i->second.busy = false;
	if (i->second.next <= now) i->second.next = now + i->second.interval;
}

qint64 Scheduler::next(const QString &id) const {
	const auto i = _jobs.find(id);
	return i == _jobs.end() ? -1 : i->second.next;
}

QStringList Scheduler::ids() const {
	auto result = QStringList();
	for (const auto &[id, job] : _jobs) result.push_back(id);
	return result;
}

} // namespace JellyPlugins::Client
