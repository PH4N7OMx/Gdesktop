/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_merged.h"

#include "history/history.h"

namespace Api {

MessagesSearchMerged::MessagesSearchMerged(not_null<History*> history)
: _apiSearch(history) {
	if (const auto migrated = history->migrateFrom()) {
		_migratedSearch.emplace(migrated);
	}
	const auto checkWaitingForTotal = [=] {
		if (_waitingForTotal) {
			if (_concatedFound.total >= 0 && _secondaryFirstFound.total >= 0) {
				_waitingForTotal = false;
				_concatedFound.total += _secondaryFirstFound.total;
				if (_isFull && !_secondaryAppended) {
					_secondaryAppended = true;
					addFound(_secondaryFirstFound);
				}
				_newFounds.fire({});
			}
		} else {
			_newFounds.fire({});
		}
	};

	const auto checkFull = [=](const FoundMessages &data) {
		if (data.total <= int(_concatedFound.messages.size())) {
			_isFull = true;
			if (!_secondaryAppended && _secondaryFirstFound.total >= 0) {
				_secondaryAppended = true;
				addFound(_secondaryFirstFound);
			}
		}
	};

	const auto handleFound = [=](bool fromMigrated, const FoundMessages &data) {
		const auto isPrimary = !_migratedSearch
			|| (_request.extraFilters.isOldestFirst()
				? fromMigrated
				: !fromMigrated);
		if (isPrimary) {
			if (data.nextToken == _concatedFound.nextToken) {
				addFound(data);
				checkFull(data);
				_nextFounds.fire({});
			} else {
				_concatedFound = data;
				checkFull(data);
				checkWaitingForTotal();
			}
		} else {
			if (_isFull && _secondaryAppended) {
				addFound(data);
			}
			if (data.nextToken == _secondaryFirstFound.nextToken) {
				_nextFounds.fire({});
			} else {
				_secondaryFirstFound = data;
				if (_isFull && !_secondaryAppended) {
					_secondaryAppended = true;
					addFound(_secondaryFirstFound);
				}
				checkWaitingForTotal();
			}
		}
	};

	_apiSearch.messagesFounds(
	) | rpl::on_next([=](const FoundMessages &data) {
		handleFound(false, data);
	}, _lifetime);

	if (_migratedSearch) {
		_migratedSearch->messagesFounds(
		) | rpl::on_next([=](const FoundMessages &data) {
			handleFound(true, data);
		}, _lifetime);
	}
}

void MessagesSearchMerged::disableMigrated() {
	_migratedSearch = std::nullopt;
	_waitingForTotal = false;
	_isFull = false;
	_secondaryAppended = false;
}

void MessagesSearchMerged::addFound(const FoundMessages &data) {
	for (const auto &message : data.messages) {
		_concatedFound.messages.push_back(message);
	}
}

const FoundMessages &MessagesSearchMerged::messages() const {
	return _concatedFound;
}

const MessagesSearch::Request &MessagesSearchMerged::request() const {
	return _request;
}

void MessagesSearchMerged::clear() {
	_concatedFound = {};
	_secondaryFirstFound = {};
	_waitingForTotal = false;
	_isFull = false;
	_secondaryAppended = false;
}

void MessagesSearchMerged::search(const Request &search) {
	_request = search;
	_isFull = false;
	_secondaryAppended = false;
	_waitingForTotal = (_migratedSearch != std::nullopt);
	if (_migratedSearch) {
		_migratedSearch->searchMessages(search);
	}
	_apiSearch.searchMessages(search);
}

void MessagesSearchMerged::searchMore() {
	if (_migratedSearch && _request.extraFilters.isOldestFirst()) {
		if (_isFull) {
			_apiSearch.searchMore();
		} else {
			_migratedSearch->searchMore();
		}
	} else if (_migratedSearch && _isFull) {
		_migratedSearch->searchMore();
	} else {
		_apiSearch.searchMore();
	}
}

rpl::producer<> MessagesSearchMerged::newFounds() const {
	return _newFounds.events();
}

rpl::producer<> MessagesSearchMerged::nextFounds() const {
	return _nextFounds.events();
}

} // namespace Api
