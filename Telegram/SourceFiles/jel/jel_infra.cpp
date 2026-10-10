// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "jel/jel_infra.h"

#include "jel/jel_lang.h"
#include "jel/jel_settings.h"
#include "jel/jel_worker.h"
#include "jel/data/jel_database.h"
#include "jel/ui/jel_logo.h"
#include "features/translator/jel_translator.h"
#include "lang/lang_instance.h"
#include "ui/chat/chat_style_radius.h"
#include "utils/rc_manager.h"

#ifdef Q_OS_WIN
#include "jel/utils/windows_utils.h"
#endif

namespace JelInfra {

void initLang() {
	QString id = Lang::GetInstance().id();
	QString baseId = Lang::GetInstance().baseId();
	if (id.isEmpty()) {
		LOG(("Language is not loaded"));
		return;
	}
	JelLanguage::init();
	JelLanguage::currentInstance()->fetchLanguage(id, baseId);
}

void initUiSettings() {
	const auto &settings = JelSettings::getInstance();
	Ui::SetAppliedBubbleRadius(settings.messageBubbleRadius());
}

void initDatabase() {
	JelDatabase::initialize();
}

void initWorker() {
	JelWorker::initialize();
}

void initRCManager() {
	RCManager::getInstance().start();
}

void initTranslator() {
	Jel::Translator::TranslateManager::init();
}

void initIcon() {
#ifdef Q_OS_WIN
	JelAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif
}

void init() {
	initLang();
	initDatabase();
	initUiSettings();
	initIcon();
	initWorker();
	initRCManager();
	initTranslator();
}

}
