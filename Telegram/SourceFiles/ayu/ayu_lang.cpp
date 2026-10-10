// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_lang.h"

#include "qjsondocument.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_instance.h"
#include "storage/localstorage.h"

#include <QDir>
#include <QFile>

// hard-coded languages
std::map<QString, QString> langMapping = {
	{"pt-br", "pt"},
	{"zh-hans-beta", "zh-hans"},
	{"zh-hant-beta", "zh-hant"},
	{"zh-hans-raw", "zh-hans"},
	{"zh-hant-raw", "zh-hant"},
};

constexpr auto postfixes = {
	"zero",
	"one",
	"two",
	"few",
	"many",
	"other"
};

AyuLanguage *AyuLanguage::instance = nullptr;

AyuLanguage::AyuLanguage() = default;

void AyuLanguage::init() {
	if (!instance) {
		instance = new AyuLanguage;
		Lang::GetInstance().updated(
		) | rpl::on_next([] {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			if (!id.isEmpty() && instance) {
				instance->loadCachedLanguage();
				instance->fetchLanguage(id, baseId);
			}
		}, instance->_lifetime);
	}
	instance->loadCachedLanguage();
}

AyuLanguage *AyuLanguage::currentInstance() {
	return instance;
}

QString AyuLanguage::getCacheDir() const {
	return cWorkingDir() + u"tdata/ayu/languages/"_q;
}

QString AyuLanguage::getCachePath(const QString &langId) const {
	return getCacheDir() + langId + u".json"_q;
}

void AyuLanguage::loadCachedLanguage() {
	const auto langPackId = Lang::GetInstance().id();
	const auto langPackBaseId = Lang::GetInstance().baseId();
	auto &language = Lang::GetInstance();
	language.resetValue("ayu_CreationDateUserConfirmed");
	language.resetValue("ayu_CreationDateSelfConfirmed");
	const auto pluginStrings = std::map<QString, QString>{
		{ u"ayu_JellyHistoryChats"_q, u"Читать историю этих чатов"_q },
		{ u"ayu_JellyReactionChats"_q, u"Ставить реакции в этих чатах"_q },
		{ u"ayu_JellyEditChats"_q, u"Редактировать исходящие сообщения в этих чатах"_q },
		{ u"ayu_JellyAttachmentChats"_q, u"Копировать вложения из этих чатов"_q },
		{ u"ayu_JellyBotChats"_q, u"Нажимать кнопки ботов в этих чатах"_q },
		{ u"ayu_JellyJoinChannels"_q, u"Вступать в эти каналы"_q },
		{ u"ayu_JellyCodeNotice"_q, u"Установленный JavaScript. Только просмотр; исходный TypeScript может отличаться от этого кода."_q },
		{ u"ayu_JellyViewCode"_q, u"Посмотреть код"_q },
		{ u"ayu_JellyDocumentation"_q, u"Документация JellyPlugins"_q },
		{ u"ayu_JellyFileRead"_q, u"Читать выбранные мной файлы"_q },
		{ u"ayu_JellyFileWrite"_q, u"Сохранять файлы в выбранное мной место"_q },
		{ u"ayu_JellyMoneyRead"_q, u"Читать мой баланс Stars / Telegram TON"_q },
		{ u"ayu_JellyWebviewBots"_q, u"Открывать Mini Apps этих ботов"_q },
		{ u"ayu_JellyFileNotice"_q, u"Для каждого чтения или сохранения ты выбираешь файл. Плагин получает содержимое, но не полный путь. Сохранение может заменить существующий файл."_q },
		{ u"ayu_JellyMiniAppsNotice"_q, u"Mini Apps получают твой профиль через Telegram. Плагин не может читать приложение, входить в него или подтверждать платежи."_q },
		{ u"ayu_JellyMenuChats"_q, u"Добавлять пункты меню и читать выбранные мной сообщения в этих чатах"_q },
		{ u"ayu_JellyDialogs"_q, u"Показывать уведомления и диалоги подтверждения"_q },
		{ u"ayu_JellySettingsNotice"_q, u"Измени настройки ниже. Сохранение останавливает плагин; включи его снова для применения изменений."_q },
		{ u"ayu_JellyInvalidSettings"_q, u"Проверь выделенные настройки и допустимые значения."_q },
		{ u"ayu_JellyEditJson"_q, u"Редактировать JSON"_q },
		{ u"ayu_JellyPlugins"_q, u"JellyPlugins"_q },
		{ u"ayu_JellyInstall"_q, u"Установить JellyPlugin"_q },
		{ u"ayu_JellyInstallNotice"_q, u"Пакет будет сохранён без запуска. Замена плагина останавливает его и требует повторного разрешения доступа."_q },
		{ u"ayu_JellyPermissions"_q, u"Разрешения плагина"_q },
		{ u"ayu_JellyPermissionNotice"_q, u"Разрешай только нужные возможности. Ключи аккаунта и системные команды недоступны."_q },
		{ u"ayu_JellyCombinedRisk"_q, u"Чтение чатов, файлов или баланса вместе с HTTPS позволяет плагину передавать эти данные разрешённым сайтам."_q },
		{ u"ayu_JellyReadChats"_q, u"Читать новые сообщения в этих чатах"_q },
		{ u"ayu_JellySendChats"_q, u"Отправлять сообщения в эти чаты"_q },
		{ u"ayu_JellyHttpHosts"_q, u"Обращаться к этим HTTPS-сайтам"_q },
		{ u"ayu_JellyStorage"_q, u"Сохранять данные плагина локально"_q },
		{ u"ayu_JellyTimers"_q, u"Работать по расписанию, пока приложение открыто"_q },
		{ u"ayu_JellyActions"_q, u"Добавлять кнопки на страницу плагинов"_q },
		{ u"ayu_JellyMessageLimit"_q, u"Максимум операций Telegram в час: "_q },
		{ u"ayu_JellyEnable"_q, u"Включить"_q },
		{ u"ayu_JellyDisable"_q, u"Выключить и отозвать разрешения"_q },
		{ u"ayu_JellyStopAll"_q, u"Остановить все плагины"_q },
		{ u"ayu_JellyConfiguration"_q, u"Настройки плагина"_q },
		{ u"ayu_JellyConfigurationNotice"_q, u"Укажи настройки JSON из инструкции автора. Сохранение останавливает плагин; включи его снова для применения изменений."_q },
		{ u"ayu_JellyLog"_q, u"Журнал действий"_q },
		{ u"ayu_JellyEmpty"_q, u"JellyPlugins пока нет. Нажми «Установить JellyPlugin» и выбери файл .jelly."_q },
		{ u"ayu_JellyInvalidPackage"_q, u"Не удалось прочитать пакет плагина (максимум 1 МиБ)."_q },
		{ u"ayu_JellyRunning"_q, u"Работает"_q },
		{ u"ayu_JellyDisabled"_q, u"Выключен"_q },
		{ u"ayu_JellyRemove"_q, u"Удалить плагин"_q },
		{ u"ayu_JellyRemoveNotice"_q, u"Удалить плагин, его настройки и сохранённые данные? Это действие нельзя отменить."_q },
	};
	for (const auto &[key, value] : pluginStrings) {
		language.resetValue(key.toUtf8());
	}
	if (langPackId == u"ru"_q || langPackBaseId == u"ru"_q) {
		for (const auto &[key, value] : pluginStrings) {
			language.applyValue(key.toUtf8(), value.toUtf8());
		}
		language.applyValue(
			"ayu_CreationDateUserConfirmed",
			u"**{item1}** создал(а) свой аккаунт **{item2}**."_q.toUtf8());
		language.applyValue(
			"ayu_CreationDateSelfConfirmed",
			u"Вы создали свой аккаунт **{item}**."_q.toUtf8());
	}
	auto finalLangPackId = langMapping.contains(langPackId) ? langMapping[langPackId] : langPackId;

	if (finalLangPackId.isEmpty()) {
		finalLangPackId = langPackBaseId;
	}
	if (finalLangPackId.isEmpty()) {
		return;
	}

	const auto cachePath = getCachePath(finalLangPackId);
	QFile file(cachePath);
	if (!file.exists()) {
		const auto basePath = getCachePath(langPackBaseId);
		if (!QFile::exists(basePath)) {
			return;
		}
		file.setFileName(basePath);
	}

	if (file.open(QIODevice::ReadOnly)) {
		const auto data = file.readAll();
		file.close();

		QJsonParseError error{};
		const auto doc = QJsonDocument::fromJson(data, &error);
		if (error.error == QJsonParseError::NoError) {
			LOG(("Loading cached GummyGram language: %1").arg(finalLangPackId));
			applyLanguageJson(doc);
		}
	}
}

void AyuLanguage::saveCachedLanguage(const QByteArray &json, const QString &langId) {
	const auto cacheDir = getCacheDir();
	QDir().mkpath(cacheDir);

	const auto cachePath = getCachePath(langId);
	QFile file(cachePath);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(json);
		file.close();
		LOG(("Cached GummyGram language: %1").arg(langId));
	}
}

void AyuLanguage::fetchLanguage(const QString &id, const QString &baseId) {
	if (_chkReply) {
		_chkReply->disconnect();
		_chkReply->abort();
		_chkReply = nullptr;
	}
	needFallback = false;

	auto finalLangPackId = langMapping.contains(id) ? langMapping[id] : id;
	_currentLangId = finalLangPackId.isEmpty() ? baseId : finalLangPackId;

	if (Core::App().settings().proxy().isEnabled()) {
		const auto proxy = Core::App().settings().proxy().selected();
		if (proxy.type == MTP::ProxyData::Type::Socks5 || proxy.type == MTP::ProxyData::Type::Http) {
			const auto networkProxy = ToNetworkProxy(ToDirectIpProxy(Core::App().settings().proxy().selected()));
			networkManager.setProxy(networkProxy);
		}
	}

	QUrl url;
	const auto targetLangId = (!finalLangPackId.isEmpty() && !needFallback)
		? finalLangPackId
		: (needFallback ? baseId : finalLangPackId);

	url.setUrl(qsl("https://raw.githubusercontent.com/PH4N7OMx/Languages/main/values/langs/%1/Shared.json").arg(
		targetLangId));

	QNetworkRequest req(url);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	_chkReply = networkManager.get(req);
	connect(_chkReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(fetchError(QNetworkReply::NetworkError)));
	connect(_chkReply, SIGNAL(finished()), this, SLOT(fetchFinished()));
}

void AyuLanguage::fetchFinished() {
	if (!_chkReply) return;

	QString langPackBaseId = Lang::GetInstance().baseId();
	QString langPackId = Lang::GetInstance().id();
	auto statusCode = _chkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

	if (statusCode == 404 && !langPackId.isEmpty() && !langPackBaseId.isEmpty() && !needFallback) {
		LOG(("GummyGram Language not found! Fallback to main language: %1...").arg(langPackBaseId));
		needFallback = true;
		_chkReply->disconnect();
		fetchLanguage("", langPackBaseId);
	} else {
		const auto result = _chkReply->readAll().trimmed();
		QJsonParseError error{};
		const auto doc = QJsonDocument::fromJson(result, &error);
		if (error.error == QJsonParseError::NoError) {
			saveCachedLanguage(result, _currentLangId);
			applyLanguageJson(doc);
		} else {
			LOG(("Incorrect language JSON File."));
		}

		_chkReply = nullptr;
	}
}

void AyuLanguage::fetchError(QNetworkReply::NetworkError e) {
	LOG(("Network error: %1").arg(e));

	if (e == QNetworkReply::NetworkError::ContentNotFoundError) {
		const auto baseId = Lang::GetInstance().baseId();
		const auto id = Lang::GetInstance().id();

		if (!id.isEmpty() && !baseId.isEmpty() && !needFallback) {
			LOG(("GummyGram Language not found! Fallback to main language: %1...").arg(baseId));
			needFallback = true;
			_chkReply->disconnect();
			fetchLanguage("", baseId);
		} else {
			LOG(("GummyGram Language not found!"));
			_chkReply = nullptr;
		}
	}
}

void AyuLanguage::applyLanguageJson(QJsonDocument doc) {
	const auto json = doc.object();
	for (const QString &brokenKey : json.keys()) {
		auto key = qsl("ayu_") + brokenKey;
		auto val = json.value(brokenKey).toString().replace(qsl("&amp;"), qsl("&"));

		if (key.endsWith("_Android")) {
			continue;
		}

		for (const auto &postfix : postfixes) {
			if (key.endsWith(qsl("_") + postfix)) {
				key = key.replace(qsl("_") + postfix, qsl("#") + postfix);
				break;
			}
		}

		if (key.endsWith("_PC")) {
			key = key.replace("_PC", "");
		}

		if (key == u"ayu_SettingsWatermark"_q
			|| key == u"ayu_ExteraChatsAlert"_q
			|| key == u"ayu_PluginsNotAvailable"_q) {
			Lang::GetInstance().resetValue(key.toUtf8());
			continue;
		}
		if (key != u"ayu_SupporterPopup"_q
			&& key != u"ayu_OfficialResourcePopup"_q) {
			val.replace(u"AyuGram Fork"_q, u"GummyGram"_q);
			val.replace(u"AyuGram"_q, u"GummyGram"_q);
			val.replace(u"Ayu"_q, u"Gummy"_q);
		}

		if (val.contains(qsl("%1$d")) && !val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{count}"));
		} else if (val.contains(qsl("%1$d")) && val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{item1}")).replace(qsl("%2$d"), qsl("{item2}"));
		} else if (val.contains(qsl("%1$s")) && !val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item}"));
		} else if (val.contains(qsl("%1$s")) && val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item1}")).replace(qsl("%2$s"), qsl("{item2}"));
		}

		Lang::GetInstance().resetValue(key.toUtf8());
		Lang::GetInstance().applyValue(key.toUtf8(), val.toUtf8());

		if (brokenKey == u"KeepDeletedMessages"_q) {
			Lang::GetInstance().applyValue("ayu_SaveDeletedMessages", val.toUtf8());
		} else if (brokenKey == u"KeepMessagesHistory"_q) {
			Lang::GetInstance().applyValue("ayu_SaveMessagesHistory", val.toUtf8());
		} else if (brokenKey == u"SaveForBots"_q) {
			Lang::GetInstance().applyValue("ayu_MessageSavingSaveForBots", val.toUtf8());
		} else if (brokenKey == u"MarkReadAfterSend"_q) {
			Lang::GetInstance().applyValue("ayu_MarkReadAfterAction", val.toUtf8());
		} else if (brokenKey == u"GhostMode"_q) {
			Lang::GetInstance().applyValue("ayu_GhostModeToggle", val.toUtf8());
		} else if (brokenKey == u"GhostModeToggle"_q) {
			Lang::GetInstance().applyValue("ayu_GhostMode", val.toUtf8());
		} else if (brokenKey == u"LocalTelegramPremium"_q) {
			Lang::GetInstance().applyValue("ayu_LocalPremium", val.toUtf8());
		} else if (brokenKey == u"LocalPremium"_q) {
			Lang::GetInstance().applyValue("ayu_LocalTelegramPremium", val.toUtf8());
		}
	}
	Lang::GetInstance().updatePluralRules();
}
