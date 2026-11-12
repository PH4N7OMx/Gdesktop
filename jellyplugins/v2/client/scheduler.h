#pragma once

#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

#include <map>

namespace JellyPlugins::Client {

class Scheduler final {
public:
	[[nodiscard]] bool every(const QString &id, int seconds, bool immediate, qint64 now, qint64 delay = -1);
	void cancel(const QString &id);
	void clear();
	[[nodiscard]] QStringList due(qint64 now);
	void complete(const QString &id, qint64 now);
	[[nodiscard]] qint64 next(const QString &id) const;
	[[nodiscard]] QStringList ids() const;

private:
	struct Job {
		qint64 interval = 0;
		qint64 next = 0;
		bool busy = false;
	};
	std::map<QString, Job> _jobs;

};

} // namespace JellyPlugins::Client
