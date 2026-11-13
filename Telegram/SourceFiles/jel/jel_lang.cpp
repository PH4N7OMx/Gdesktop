// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "jel/jel_lang.h"

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

JelLanguage *JelLanguage::instance = nullptr;

JelLanguage::JelLanguage() = default;

void JelLanguage::init() {
	if (!instance) {
		instance = new JelLanguage;
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

JelLanguage *JelLanguage::currentInstance() {
	return instance;
}

QString JelLanguage::getCacheDir() const {
	return cWorkingDir() + u"tdata/jel/languages/"_q;
}

QString JelLanguage::getCachePath(const QString &langId) const {
	return getCacheDir() + langId + u".json"_q;
}

void JelLanguage::loadCachedLanguage() {
	const auto langPackId = Lang::GetInstance().id();
	const auto langPackBaseId = Lang::GetInstance().baseId();
	auto &language = Lang::GetInstance();
	language.resetValue("jel_CreationDateUserConfirmed");
	language.resetValue("jel_CreationDateSelfConfirmed");
	const auto pluginStrings = std::map<QString, QString>{
		{ u"jel_SwitchStyle"_q, u"Стиль переключателей"_q },
		{ u"jel_SwitchStyleClassic"_q, u"Классический"_q },
		{ u"jel_SwitchStyleRounded"_q, u"Скруглённый"_q },
		{ u"jel_SwitchStylePreview"_q, u"Попробуйте переключатели ниже. Стиль применяется сразу."_q },
		{ u"jel_TranslatorHeader"_q, u"Перевод"_q },
		{ u"jel_JellyConfiguration"_q, u"Настройки плагина"_q },
		{ u"jel_JellyDisabled"_q, u"Выключен"_q },
		{ u"jel_JellyDocumentation"_q, u"Документация JellyPlugins"_q },
		{ u"jel_JellyEmpty"_q, u"JellyPlugins пока нет. Нажми «Установить JellyPlugin» и выбери файл .jelly."_q },
		{ u"jel_JellyEnable"_q, u"Включить"_q },
		{ u"jel_JellyInstall"_q, u"Установить JellyPlugin"_q },
		{ u"jel_JellyInstallNotice"_q, u"Пакет будет сохранён без запуска. Замена плагина останавливает его и требует повторного разрешения доступа."_q },
		{ u"jel_JellyInvalidPackage"_q, u"Не удалось прочитать пакет плагина (максимум 1 МиБ)."_q },
		{ u"jel_JellyLog"_q, u"Журнал действий"_q },
		{ u"jel_JellyPlugins"_q, u"Плагины"_q },
		{ u"jel_JellyRemove"_q, u"Удалить плагин"_q },
		{ u"jel_JellyRunning"_q, u"Работает"_q },
		{ u"jel_JellyStopAll"_q, u"Остановить все плагины"_q },
		{ u"jel_JellyV2Account"_q, u"Аккаунт: "_q },
		{ u"jel_JellyV2Activating"_q, u"Активируется"_q },
		{ u"jel_JellyV2ActualNotice"_q, u"Клиент показал параметры фактического запроса API. Описание автора не используется для подтверждения действия."_q },
		{ u"jel_JellyV2Affected"_q, u"Плагинов этого аккаунта с глобальным режимом: "_q },
		{ u"jel_JellyV2Allow"_q, u"Разрешить"_q },
		{ u"jel_JellyV2AppOpen"_q, u"Плагины работают, пока открыт GummyGram."_q },
		{ u"jel_JellyV2ClearRules"_q, u"Удалить все сохранённые правила и запреты"_q },
		{ u"jel_JellyV2Deny"_q, u"Запретить"_q },
		{ u"jel_JellyV2Disable"_q, u"Выключить"_q },
		{ u"jel_JellyV2Failed"_q, u"Остановлен с ошибкой"_q },
		{ u"jel_JellyV2FullNotice"_q, u"Разрешить отправку текста и чтение истории чатов без отдельных подтверждений. Сохранённые запреты, лимиты и изоляция продолжают действовать."_q },
		{ u"jel_JellyV2FuturePlugins"_q, u"Это касается и будущих плагинов с глобальным режимом после их включения."_q },
		{ u"jel_JellyV2GlobalMode"_q, u"Глобальный доступ плагинов"_q },
		{ u"jel_JellyV2GlobalNotice"_q, u"Действует для всех аккаунтов и плагинов с глобальным режимом. Личные режимы имеют приоритет."_q },
		{ u"jel_JellyV2LocalPackage"_q, u"Локальный пакет · автор не проверен"_q },
		{ u"jel_JellyV2ModeAsk"_q, u"Спрашивать перед каждым действием"_q },
		{ u"jel_JellyV2ModeFull"_q, u"Полный доступ к API клиента"_q },
		{ u"jel_JellyV2ModeInherit"_q, u"Использовать глобальный режим"_q },
		{ u"jel_JellyV2ModeNotice"_q, u"Выбери доступ плагина к API клиента. Сохранённые запреты действуют и при полном доступе."_q },
		{ u"jel_JellyV2ModeRemember"_q, u"Использовать сохранённые правила"_q },
		{ u"jel_JellyV2NeedsApproval"_q, u"Плагину нужно разрешение. Открой «Плагины → Ожидающие действия», чтобы проверить запрос."_q },
		{ u"jel_JellyV2NextRun"_q, u"Следующее событие расписания: "_q },
		{ u"jel_JellyV2NoPending"_q, u"Нет действий, требующих подтверждения."_q },
		{ u"jel_JellyV2NoPreview"_q, u"Предпросмотр ссылок отключён"_q },
		{ u"jel_JellyV2NoRules"_q, u"Сохранённых правил нет. Действует выбранный режим доступа."_q },
		{ u"jel_JellyV2Notify"_q, u"Обычные настройки уведомлений"_q },
		{ u"jel_JellyV2Pending"_q, u"Ожидающие действия"_q },
		{ u"jel_JellyV2PluginMode"_q, u"Доступ плагина"_q },
		{ u"jel_JellyV2ReadHistory"_q, u"Прочитать историю чата. Максимум сообщений: "_q },
		{ u"jel_JellyV2RememberChat"_q, u"При запоминании разрешения разрешить любой текст в этом чате с таким же режимом уведомления"_q },
		{ u"jel_JellyV2RememberExact"_q, u"Запомнить это точное действие или отказ"_q },
		{ u"jel_JellyV2RememberNotice"_q, u"При запоминании разрешения личный режим при необходимости переключится на правила. Закрытие окна оставляет запрос в ожидании до истечения срока."_q },
		{ u"jel_JellyV2RemoveData"_q, u"Также удалить настройки и данные этого плагина. Разрешения удаляются в любом случае."_q },
		{ u"jel_JellyV2RequestedBy"_q, u"Запросил плагин: "_q },
		{ u"jel_JellyV2ReviewAction"_q, u"Подтверждение действия"_q },
		{ u"jel_JellyV2Rollback"_q, u"Вернуть предыдущую ревизию"_q },
		{ u"jel_JellyV2RollbackNotice"_q, u"Восстановить предыдущий пакет и его снимок данных? Плагин остановится; его нужно включить заново. Текущие разрешения будут сброшены."_q },
		{ u"jel_JellyV2Rules"_q, u"Сохранённые правила и запреты"_q },
		{ u"jel_JellyV2SendText"_q, u"Отправить этот текст:"_q },
		{ u"jel_JellyV2SettingsNotice"_q, u"Настройки плагина в JSON. Сохранение перезапустит включённый плагин. Действия проверяются по их фактическим параметрам."_q },
		{ u"jel_JellyV2Silent"_q, u"Отправить без уведомления"_q },
		{ u"jel_JellyV2Starting"_q, u"Запускается"_q },
		{ u"jel_JellyV2Stopping"_q, u"Останавливается"_q },
		{ u"jel_JellyV2Waiting"_q, u"Ожидает подтверждения"_q },
		{ u"jel_JellyViewCode"_q, u"Посмотреть код"_q },
	};
	for (const auto &[key, value] : pluginStrings) {
		language.resetValue(key.toUtf8());
	}
	if (langPackId == u"ru"_q || langPackBaseId == u"ru"_q) {
		for (const auto &[key, value] : pluginStrings) {
			language.applyValue(key.toUtf8(), value.toUtf8());
		}
		language.applyValue(
			"jel_CreationDateUserConfirmed",
			u"**{item1}** создал(а) свой аккаунт **{item2}**."_q.toUtf8());
		language.applyValue(
			"jel_CreationDateSelfConfirmed",
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
		const auto legacy = cWorkingDir() + u"tdata/ayu/languages/"_q + finalLangPackId + u".json"_q;
		if (QFile::exists(legacy)) file.setFileName(legacy);
	}
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

void JelLanguage::saveCachedLanguage(const QByteArray &json, const QString &langId) {
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

void JelLanguage::fetchLanguage(const QString &id, const QString &baseId) {
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

void JelLanguage::fetchFinished() {
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

void JelLanguage::fetchError(QNetworkReply::NetworkError e) {
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

void JelLanguage::applyLanguageJson(QJsonDocument doc) {
	const auto json = doc.object();
	for (const QString &brokenKey : json.keys()) {
		auto normalizedKey = brokenKey;
		normalizedKey.replace(u"AyuGram"_q, u"GummyGram"_q);
		normalizedKey.replace(u"Ayu"_q, u"Jel"_q);
		auto key = qsl("jel_") + normalizedKey;
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

		if (key == u"jel_SettingsWatermark"_q
			|| key == u"jel_ExteraChatsAlert"_q
			|| key == u"jel_PluginsNotAvailable"_q) {
			Lang::GetInstance().resetValue(key.toUtf8());
			continue;
		}
		if (key != u"jel_SupporterPopup"_q
			&& key != u"jel_OfficialResourcePopup"_q) {
			val.replace(u"GummyGram Fork"_q, u"GummyGram"_q);
			val.replace(u"AyuGram"_q, u"GummyGram"_q);
			val.replace(u"Ayu"_q, u"Jel"_q);
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
			Lang::GetInstance().applyValue("jel_SaveDeletedMessages", val.toUtf8());
		} else if (brokenKey == u"KeepMessagesHistory"_q) {
			Lang::GetInstance().applyValue("jel_SaveMessagesHistory", val.toUtf8());
		} else if (brokenKey == u"SaveForBots"_q) {
			Lang::GetInstance().applyValue("jel_MessageSavingSaveForBots", val.toUtf8());
		} else if (brokenKey == u"MarkReadAfterSend"_q) {
			Lang::GetInstance().applyValue("jel_MarkReadAfterAction", val.toUtf8());
		} else if (brokenKey == u"GhostMode"_q) {
			Lang::GetInstance().applyValue("jel_GhostModeToggle", val.toUtf8());
		} else if (brokenKey == u"GhostModeToggle"_q) {
			Lang::GetInstance().applyValue("jel_GhostMode", val.toUtf8());
		} else if (brokenKey == u"LocalTelegramPremium"_q) {
			Lang::GetInstance().applyValue("jel_LocalPremium", val.toUtf8());
		} else if (brokenKey == u"LocalPremium"_q) {
			Lang::GetInstance().applyValue("jel_LocalTelegramPremium", val.toUtf8());
		}
	}
	Lang::GetInstance().updatePluralRules();
}
