#pragma once

#include "client/contract.h"

#include <QObject>

#include <functional>
#include <map>
#include <memory>

namespace Main {
class Session;
} // namespace Main

namespace JellyPlugins {

struct TelegramResult {
	QJsonValue value;
	QString error;
	int retryAfter = 0;
};

class TelegramApi final : public QObject {
public:
	explicit TelegramApi(not_null<Main::Session*> session);
	~TelegramApi();
	[[nodiscard]] QString recipient(const Client::Action &action) const;
	void execute(const QString &token, const Client::Action &action, std::function<void(TelegramResult)> complete);
	void cancel(const QString &token);

private:
	struct Operation;
	void cleanup(const std::shared_ptr<Operation> &operation, bool removeLocal);
	const not_null<Main::Session*> _session;
	std::map<QString, std::shared_ptr<Operation>> _operations;

};

} // namespace JellyPlugins
