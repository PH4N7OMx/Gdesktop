#include "jel/ui/settings/settings_plugins.h"

#include "jel/plugins/plugin_manager.h"
#include "jel/ui/settings/settings_main.h"
#include "lang/lang_text_entity.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/basic_click_handlers.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "lang_auto.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QTextEdit>
#include <tuple>
#include <cmath>

namespace Settings {
namespace {

using namespace Builder;
using JellyPlugins::PluginInfo;

void AddLabel(not_null<Ui::GenericBox*> box, const QString &text) {
	box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(text), st::boxLabel));
}

void ShowPermissions(not_null<Window::SessionController*> controller, PluginInfo info) {
	const auto manager = QPointer<JellyPlugins::Manager>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::jel_JellyPermissions());
		box->setWidth(st::boxWideWidth);
		AddLabel(box, info.package.name + u" · "_q + info.package.version);
		AddLabel(box, tr::jel_JellyPermissionNotice(tr::now));
		if ((!info.package.permissions.readChats.isEmpty()
			|| !info.package.permissions.historyChats.isEmpty()
			|| info.package.permissions.fileRead || info.package.permissions.moneyRead
			|| !info.package.permissions.menuChats.isEmpty())
			&& !info.package.permissions.httpHosts.isEmpty()) {
			AddLabel(box, tr::jel_JellyCombinedRisk(tr::now));
		}
		const auto checks = std::make_shared<std::map<QString, std::vector<Ui::Checkbox*>>>();
		const auto addScopes = [&](const QString &title, const QString &key, const QStringList &values, bool checked = true) {
			if (values.isEmpty()) return;
			AddLabel(box, title);
			for (const auto &value : values) {
				const auto check = box->addRow(object_ptr<Ui::Checkbox>(
					box, value, checked, st::defaultCheckbox));
				check->setChecked(checked);
				(*checks)[key].push_back(check);
			}
		};
		addScopes(tr::jel_JellyMenuChats(tr::now), u"menuChats"_q, info.package.permissions.menuChats, false);
		addScopes(tr::jel_JellyReadChats(tr::now), u"readChats"_q, info.package.permissions.readChats);
		addScopes(tr::jel_JellySendChats(tr::now), u"sendChats"_q, info.package.permissions.sendChats);
		addScopes(tr::jel_JellyJoinChannels(tr::now), u"joinChannels"_q, info.package.permissions.joinChannels);
		addScopes(tr::jel_JellyBotChats(tr::now), u"botChats"_q, info.package.permissions.botChats);
		addScopes(tr::jel_JellyAttachmentChats(tr::now), u"attachmentChats"_q, info.package.permissions.attachmentChats);
		addScopes(tr::jel_JellyEditChats(tr::now), u"editChats"_q, info.package.permissions.editChats);
		addScopes(tr::jel_JellyReactionChats(tr::now), u"reactionChats"_q, info.package.permissions.reactionChats);
		addScopes(tr::jel_JellyHistoryChats(tr::now), u"historyChats"_q, info.package.permissions.historyChats);
		if (info.package.permissions.fileRead || info.package.permissions.fileWrite) {
			AddLabel(box, tr::jel_JellyFileNotice(tr::now));
		}
		if (!info.package.permissions.webviewBots.isEmpty()) {
			AddLabel(box, tr::jel_JellyMiniAppsNotice(tr::now));
		}
		addScopes(tr::jel_JellyWebviewBots(tr::now), u"webviewBots"_q, info.package.permissions.webviewBots, false);
		addScopes(tr::jel_JellyHttpHosts(tr::now), u"httpHosts"_q, info.package.permissions.httpHosts);
		for (const auto &[key, allowed, title] : {
			std::tuple(u"storage"_q, info.package.permissions.storage, tr::jel_JellyStorage(tr::now)),
			std::tuple(u"timers"_q, info.package.permissions.timers, tr::jel_JellyTimers(tr::now)),
			std::tuple(u"ui"_q, info.package.permissions.ui, tr::jel_JellyActions(tr::now)),
			std::tuple(u"fileRead"_q, info.package.permissions.fileRead, tr::jel_JellyFileRead(tr::now)),
			std::tuple(u"fileWrite"_q, info.package.permissions.fileWrite, tr::jel_JellyFileWrite(tr::now)),
			std::tuple(u"moneyRead"_q, info.package.permissions.moneyRead, tr::jel_JellyMoneyRead(tr::now)),
			std::tuple(u"uiDialogs"_q, info.package.permissions.uiDialogs, tr::jel_JellyDialogs(tr::now)),
		}) {
			if (!allowed) continue;
			const auto check = box->addRow(object_ptr<Ui::Checkbox>(box, title, true, st::defaultCheckbox));
			check->setChecked(key == u"storage"_q || key == u"timers"_q || key == u"ui"_q);
			(*checks)[key].push_back(check);
		}
		AddLabel(box, tr::jel_JellyMessageLimit(tr::now)
			+ QString::number(info.package.permissions.maxMessagesPerHour));
		box->addButton(tr::jel_JellyEnable(), [=] {
			if (!manager) return;
			auto grant = info.package.permissions;
			const auto selected = [&](const QString &key, const QStringList &values) {
				auto result = QStringList();
				for (auto i = 0; i < values.size(); ++i) {
					if ((*checks)[key][i]->checked()) result.push_back(values[i]);
				}
				return result;
			};
			grant.readChats = selected(u"readChats"_q, grant.readChats);
			grant.sendChats = selected(u"sendChats"_q, grant.sendChats);
			grant.joinChannels = selected(u"joinChannels"_q, grant.joinChannels);
			grant.botChats = selected(u"botChats"_q, grant.botChats);
			grant.attachmentChats = selected(u"attachmentChats"_q, grant.attachmentChats);
			grant.editChats = selected(u"editChats"_q, grant.editChats);
			grant.reactionChats = selected(u"reactionChats"_q, grant.reactionChats);
			grant.historyChats = selected(u"historyChats"_q, grant.historyChats);
			grant.menuChats = selected(u"menuChats"_q, grant.menuChats);
			grant.uiDialogs = grant.uiDialogs && (*checks)[u"uiDialogs"_q][0]->checked();
			grant.webviewBots = selected(u"webviewBots"_q, grant.webviewBots);
			grant.fileRead = grant.fileRead && (*checks)[u"fileRead"_q][0]->checked();
			grant.fileWrite = grant.fileWrite && (*checks)[u"fileWrite"_q][0]->checked();
			grant.moneyRead = grant.moneyRead && (*checks)[u"moneyRead"_q][0]->checked();
			grant.httpHosts = selected(u"httpHosts"_q, grant.httpHosts);
			grant.storage = grant.storage && (*checks)[u"storage"_q][0]->checked();
			grant.timers = grant.timers && (*checks)[u"timers"_q][0]->checked();
			grant.ui = grant.ui && (*checks)[u"ui"_q][0]->checked();
			auto error = QString();
			if (!manager->enable(info.package.id, info.package.digest, grant, error)) {
				controller->showToast(error);
			} else {
				box->closeBox();
			}
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowJsonConfiguration(not_null<Window::SessionController*> controller, PluginInfo info) {
	const auto manager = QPointer<JellyPlugins::Manager>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::jel_JellyConfiguration());
		box->setWidth(st::boxWideWidth);
		AddLabel(box, tr::jel_JellyConfigurationNotice(tr::now));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box, st::defaultInputField, Ui::InputField::Mode::MultiLine,
			rpl::single(u"JSON"_q), QString::fromUtf8(QJsonDocument(info.settings).toJson())));
		box->addButton(tr::lng_settings_save(), [=] {
			if (!manager) return;
			auto parseError = QJsonParseError();
			const auto document = QJsonDocument::fromJson(field->getLastText().toUtf8(), &parseError);
			if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
				field->showError();
				return;
			}
			auto error = QString();
			if (manager->configure(info.package.id, document.object(), error)) {
				box->closeBox();
			} else {
				controller->showToast(error);
			}
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowConfiguration(not_null<Window::SessionController*> controller, PluginInfo info) {
	if (info.package.settingsSchema.isEmpty()) {
		ShowJsonConfiguration(controller, info);
		return;
	}
	const auto manager = QPointer<JellyPlugins::Manager>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::jel_JellyConfiguration());
		box->setWidth(st::boxWideWidth);
		AddLabel(box, tr::jel_JellySettingsNotice(tr::now));
		const auto values = std::make_shared<std::vector<std::pair<QString, Fn<QJsonValue()>>>>();
		for (const auto &row : info.package.settingsSchema) {
			const auto definition = row.toObject();
			const auto key = definition[u"key"_q].toString();
			const auto label = definition[u"label"_q].toString();
			const auto type = definition[u"type"_q].toString();
			const auto initial = info.settings.contains(key) ? info.settings.value(key) : definition[u"default"_q];
			if (type == u"boolean"_q) {
				const auto check = box->addRow(object_ptr<Ui::Checkbox>(
					box, label, initial.toBool(definition[u"default"_q].toBool()), st::defaultCheckbox));
				values->emplace_back(key, [=] { return QJsonValue(check->checked()); });
			} else if (type == u"select"_q) {
				AddLabel(box, label);
				const auto options = definition[u"options"_q].toArray();
				auto index = 0;
				for (auto i = 0; i < options.size(); ++i) {
					if (options[i] == initial) index = i;
				}
				const auto group = std::make_shared<Ui::RadiobuttonGroup>(index);
				for (auto i = 0; i < options.size(); ++i) {
					box->addRow(object_ptr<Ui::Radiobutton>(
						box, group, i, options[i].toString(), st::defaultCheckbox));
				}
				values->emplace_back(key, [=] { return options[group->current()]; });
			} else {
				AddLabel(box, label);
				const auto number = (type == u"number"_q);
				const auto initialText = number
					? QString::number(initial.toDouble(definition[u"default"_q].toDouble()), 'g', 17)
					: initial.toString(definition[u"default"_q].toString());
				const auto field = box->addRow(object_ptr<Ui::InputField>(
					box, st::defaultInputField, Ui::InputField::Mode::SingleLine,
					rpl::single(label), initialText));
				values->emplace_back(key, [=]() -> QJsonValue {
					const auto text = field->getLastText();
					if (!number) {
						if (text.size() <= definition[u"maxLength"_q].toInt(512)) return text;
					} else {
						auto valid = false;
						const auto value = text.toDouble(&valid);
						if (valid && std::isfinite(value)
							&& value >= definition[u"min"_q].toDouble(-1e9)
							&& value <= definition[u"max"_q].toDouble(1e9)) return value;
					}
					field->showError();
					return QJsonValue(QJsonValue::Undefined);
				});
			}
		}
		box->addButton(tr::lng_settings_save(), [=] {
			if (!manager) return;
			auto settings = info.settings;
			for (const auto &[key, get] : *values) {
				const auto value = get();
				if (value.isUndefined()) {
					controller->showToast(tr::jel_JellyInvalidSettings(tr::now));
					return;
				}
				settings[key] = value;
			}
			auto error = QString();
			if (manager->configure(info.package.id, settings, error)) box->closeBox();
			else controller->showToast(error);
		});
		box->addLeftButton(tr::jel_JellyEditJson(), [=] {
			box->closeBox();
			ShowJsonConfiguration(controller, info);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowDetails(not_null<Window::SessionController*> controller, PluginInfo info) {
	const auto manager = QPointer<JellyPlugins::Manager>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(info.package.name));
		box->setWidth(st::boxWideWidth);
		AddLabel(box, info.package.author + u" · "_q + info.package.version);
		AddLabel(box, info.package.description);
		AddLabel(box, info.status);
		if (info.enabled) {
			AddLabel(box, tr::jel_JellyPermissions(tr::now));
			AddLabel(box, tr::jel_JellyReadChats(tr::now) + u": "_q + info.granted.readChats.join(u", "_q));
			AddLabel(box, tr::jel_JellySendChats(tr::now) + u": "_q + info.granted.sendChats.join(u", "_q));
			AddLabel(box, tr::jel_JellyJoinChannels(tr::now) + u": "_q + info.granted.joinChannels.join(u", "_q));
			AddLabel(box, tr::jel_JellyBotChats(tr::now) + u": "_q + info.granted.botChats.join(u", "_q));
			AddLabel(box, tr::jel_JellyAttachmentChats(tr::now) + u": "_q + info.granted.attachmentChats.join(u", "_q));
			AddLabel(box, tr::jel_JellyEditChats(tr::now) + u": "_q + info.granted.editChats.join(u", "_q));
			AddLabel(box, tr::jel_JellyReactionChats(tr::now) + u": "_q + info.granted.reactionChats.join(u", "_q));
			AddLabel(box, tr::jel_JellyHistoryChats(tr::now) + u": "_q + info.granted.historyChats.join(u", "_q));
			if (info.granted.uiDialogs) AddLabel(box, tr::jel_JellyDialogs(tr::now));
			AddLabel(box, tr::jel_JellyMenuChats(tr::now) + u": "_q + info.granted.menuChats.join(u", "_q));
			if (info.granted.fileRead) AddLabel(box, tr::jel_JellyFileRead(tr::now));
			if (info.granted.fileWrite) AddLabel(box, tr::jel_JellyFileWrite(tr::now));
			if (info.granted.moneyRead) AddLabel(box, tr::jel_JellyMoneyRead(tr::now));
			AddLabel(box, tr::jel_JellyWebviewBots(tr::now) + u": "_q + info.granted.webviewBots.join(u", "_q));
			AddLabel(box, tr::jel_JellyHttpHosts(tr::now) + u": "_q + info.granted.httpHosts.join(u", "_q));
		}
		box->addButton(info.enabled ? tr::jel_JellyDisable() : tr::jel_JellyEnable(), [=] {
			if (!manager) return;
			box->closeBox();
			if (info.enabled) {
				auto error = QString();
				if (!manager->disable(info.package.id, error)) controller->showToast(error);
			} else {
				ShowPermissions(controller, info);
			}
		});
		box->addButton(tr::jel_JellyConfiguration(), [=] {
			box->closeBox();
			ShowConfiguration(controller, info);
		});
		box->addRow(object_ptr<Ui::SettingsButton>(box, tr::jel_JellyViewCode(), st::settingsButton))
			->setClickedCallback([=] {
				controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> codeBox) {
					codeBox->setTitle(tr::jel_JellyViewCode());
					codeBox->setWidth(st::boxWideWidth);
					AddLabel(codeBox, tr::jel_JellyCodeNotice(tr::now));
					const auto field = codeBox->addRow(object_ptr<Ui::InputField>(
						codeBox, st::defaultInputField, Ui::InputField::Mode::MultiLine,
						rpl::single(QString()), info.package.code));
					field->setMinHeight(st::boxWideWidth / 2);
					field->setMaxHeight(st::boxWideWidth / 2);
					field->rawTextEdit()->setReadOnly(true);
					field->rawTextEdit()->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
					codeBox->addButton(tr::lng_close(), [=] { codeBox->closeBox(); });
				}));
			});
		box->addButton(tr::jel_JellyLog(), [=] {
			controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> logBox) {
				logBox->setTitle(tr::jel_JellyLog());
				logBox->setWidth(st::boxWideWidth);
				if (!manager) return;
				for (const auto &current : manager->plugins()) {
					if (current.package.id == info.package.id) {
						AddLabel(logBox, current.log.join('\n'));
					}
				}
				logBox->addButton(tr::lng_close(), [=] { logBox->closeBox(); });
			}));
		});
		box->addLeftButton(tr::jel_JellyRemove(), [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::jel_JellyRemoveNotice(tr::marked),
				.confirmed = [=](Fn<void()> &&close) {
					if (!manager) return;
					auto error = QString();
					if (manager->uninstall(info.package.id, error)) box->closeBox();
					else controller->showToast(error);
					close();
				},
				.confirmText = tr::jel_JellyRemove(),
			}));
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

const auto kMeta = BuildHelper({
	.id = JelPlugins::Id(),
	.parentId = JelMain::Id(),
	.title = &tr::jel_JellyPlugins,
	.icon = &st::menuIconBot,
}, [](SectionBuilder &builder) {
	const auto controller = builder.controller();
	if (!controller) {
		return;
	}
	const auto manager = QPointer<JellyPlugins::Manager>(&controller->session().plugins());
	builder.addSkip();
	builder.addButton({
		.id = u"jelly/plugins/install"_q,
		.title = tr::jel_JellyInstall(),
		.icon = { &st::menuIconFile },
		.onClick = [=] {
			const auto path = QFileDialog::getOpenFileName(nullptr,
				tr::jel_JellyInstall(tr::now), {}, u"JellyPlugins (*.jelly *.jellyplugin)"_q);
			if (path.isEmpty() || !manager) return;
			auto file = QFile(path);
			if (!file.open(QIODevice::ReadOnly) || file.size() > JellyPlugins::kPackageLimit) {
				controller->showToast(tr::jel_JellyInvalidPackage(tr::now));
				return;
			}
			const auto data = file.read(JellyPlugins::kPackageLimit + 1);
			auto package = JellyPlugins::Package();
			auto error = QString();
			if (!JellyPlugins::ParsePackage(data, package, error)) {
				controller->showToast(error);
				return;
			}
			controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(tr::jel_JellyInstall());
				AddLabel(box, package.name + u" · "_q + package.version + u"\n"_q + package.author);
				AddLabel(box, package.description);
				AddLabel(box, tr::jel_JellyInstallNotice(tr::now));
				box->addButton(tr::jel_JellyInstall(), [=] {
					if (!manager) return;
					auto installError = QString();
					if (manager->install(data, installError)) box->closeBox();
					else controller->showToast(installError);
				});
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}));
		},
	});
	builder.addButton({
		.id = u"jelly/plugins/stop"_q,
		.title = tr::jel_JellyStopAll(),
		.icon = { &st::menuIconCancel },
		.onClick = [=] { if (manager) manager->stopAll(); },
	});
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addButton({
		.id = u"jelly/plugins/documentation"_q,
		.title = tr::jel_JellyDocumentation(),
		.icon = { &st::menuIconInfo },
		.onClick = [=] {
			UrlClickHandler::Open(u"https://jellygram.gitbook.io/jelly-plugins/"_q);
		},
	});
	builder.addSkip();
	builder.addSubsectionTitle(tr::jel_JellyPlugins());
	const auto plugins = manager->plugins();
	if (plugins.empty()) {
		builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
			return { .widget = object_ptr<Ui::FlatLabel>(ctx.container,
				tr::jel_JellyEmpty(), st::boxLabel),
				.margin = QMargins(st::settingsCheckboxPadding.left(), 0,
					st::settingsCheckboxPadding.left(), st::settingsCheckboxPadding.bottom()) };
		});
	}
	for (const auto &info : plugins) {
		builder.addButton({
			.id = u"jelly/plugin/"_q + info.package.id,
			.title = rpl::single(info.package.name),
			.icon = { &st::menuIconBot },
			.label = rpl::single(info.enabled ? tr::jel_JellyRunning(tr::now) : tr::jel_JellyDisabled(tr::now)),
			.onClick = [=] { ShowDetails(controller, info); },
		});
		for (auto action = info.actions.begin(); action != info.actions.end(); ++action) {
			const auto id = action.key();
			builder.addButton({
				.id = u"jelly/action/"_q + info.package.id + '/' + id,
				.title = rpl::single(action.value().toString()),
				.icon = { &st::menuIconBotCommands },
				.onClick = [=] { if (manager) manager->runAction(info.package.id, id); },
			});
		}
	}
});

} // namespace

JelPlugins::JelPlugins(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	_content = Ui::CreateChild<Ui::VerticalLayout>(this);
	refresh();
	Ui::ResizeFitChild(this, _content);
	controller->session().plugins().changes() | rpl::on_next([=] {
		if (_refreshPending) return;
		_refreshPending = true;
		QTimer::singleShot(0, this, [=] {
			_refreshPending = false;
			refresh();
		});
	}, lifetime());
}

rpl::producer<QString> JelPlugins::title() {
	return tr::jel_JellyPlugins();
}

void JelPlugins::refresh() {
	_content->clear();
	build(_content, kMeta.build);
}

} // namespace Settings
