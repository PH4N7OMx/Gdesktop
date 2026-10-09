#include "ayu/ui/settings/settings_plugins.h"

#include "ayu/plugins/plugin_manager.h"
#include "ayu/ui/settings/settings_main.h"
#include "lang/lang_text_entity.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/checkbox.h"
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
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <tuple>

namespace Settings {
namespace {

using namespace Builder;
using GummyPlugins::PluginInfo;

void AddLabel(not_null<Ui::GenericBox*> box, const QString &text) {
	box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(text), st::boxLabel));
}

void ShowPermissions(not_null<Window::SessionController*> controller, PluginInfo info) {
	const auto manager = QPointer<GummyPlugins::Manager>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::ayu_JellyPermissions());
		box->setWidth(st::boxWideWidth);
		AddLabel(box, info.package.name + u" · "_q + info.package.version);
		AddLabel(box, tr::ayu_JellyPermissionNotice(tr::now));
		if (!info.package.permissions.readChats.isEmpty()
			&& !info.package.permissions.httpHosts.isEmpty()) {
			AddLabel(box, tr::ayu_JellyCombinedRisk(tr::now));
		}
		const auto checks = std::make_shared<std::map<QString, std::vector<Ui::Checkbox*>>>();
		const auto addScopes = [&](const QString &title, const QString &key, const QStringList &values) {
			if (values.isEmpty()) return;
			AddLabel(box, title);
			for (const auto &value : values) {
				const auto check = box->addRow(object_ptr<Ui::Checkbox>(
					box, value, true, st::defaultCheckbox));
				check->setChecked(true);
				(*checks)[key].push_back(check);
			}
		};
		addScopes(tr::ayu_JellyReadChats(tr::now), u"readChats"_q, info.package.permissions.readChats);
		addScopes(tr::ayu_JellySendChats(tr::now), u"sendChats"_q, info.package.permissions.sendChats);
		addScopes(tr::ayu_JellyHttpHosts(tr::now), u"httpHosts"_q, info.package.permissions.httpHosts);
		for (const auto &[key, allowed, title] : {
			std::tuple(u"storage"_q, info.package.permissions.storage, tr::ayu_JellyStorage(tr::now)),
			std::tuple(u"timers"_q, info.package.permissions.timers, tr::ayu_JellyTimers(tr::now)),
			std::tuple(u"ui"_q, info.package.permissions.ui, tr::ayu_JellyActions(tr::now)),
		}) {
			if (!allowed) continue;
			const auto check = box->addRow(object_ptr<Ui::Checkbox>(box, title, true, st::defaultCheckbox));
			check->setChecked(true);
			(*checks)[key].push_back(check);
		}
		AddLabel(box, tr::ayu_JellyMessageLimit(tr::now)
			+ QString::number(info.package.permissions.maxMessagesPerHour));
		box->addButton(tr::ayu_JellyEnable(), [=] {
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

void ShowConfiguration(not_null<Window::SessionController*> controller, PluginInfo info) {
	const auto manager = QPointer<GummyPlugins::Manager>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::ayu_JellyConfiguration());
		box->setWidth(st::boxWideWidth);
		AddLabel(box, tr::ayu_JellyConfigurationNotice(tr::now));
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

void ShowDetails(not_null<Window::SessionController*> controller, PluginInfo info) {
	const auto manager = QPointer<GummyPlugins::Manager>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(info.package.name));
		box->setWidth(st::boxWideWidth);
		AddLabel(box, info.package.author + u" · "_q + info.package.version);
		AddLabel(box, info.package.description);
		AddLabel(box, info.status);
		if (info.enabled) {
			AddLabel(box, tr::ayu_JellyPermissions(tr::now));
			AddLabel(box, tr::ayu_JellyReadChats(tr::now) + u": "_q + info.granted.readChats.join(u", "_q));
			AddLabel(box, tr::ayu_JellySendChats(tr::now) + u": "_q + info.granted.sendChats.join(u", "_q));
			AddLabel(box, tr::ayu_JellyHttpHosts(tr::now) + u": "_q + info.granted.httpHosts.join(u", "_q));
		}
		box->addButton(info.enabled ? tr::ayu_JellyDisable() : tr::ayu_JellyEnable(), [=] {
			if (!manager) return;
			box->closeBox();
			if (info.enabled) {
				auto error = QString();
				if (!manager->disable(info.package.id, error)) controller->showToast(error);
			} else {
				ShowPermissions(controller, info);
			}
		});
		box->addButton(tr::ayu_JellyConfiguration(), [=] {
			box->closeBox();
			ShowConfiguration(controller, info);
		});
		box->addButton(tr::ayu_JellyLog(), [=] {
			controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> logBox) {
				logBox->setTitle(tr::ayu_JellyLog());
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
		box->addLeftButton(tr::ayu_JellyRemove(), [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_JellyRemoveNotice(tr::marked),
				.confirmed = [=](Fn<void()> &&close) {
					if (!manager) return;
					auto error = QString();
					if (manager->uninstall(info.package.id, error)) box->closeBox();
					else controller->showToast(error);
					close();
				},
				.confirmText = tr::ayu_JellyRemove(),
			}));
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

const auto kMeta = BuildHelper({
	.id = AyuPlugins::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_JellyPlugins,
	.icon = &st::menuIconBot,
}, [](SectionBuilder &) {});

} // namespace

AyuPlugins::AyuPlugins(QWidget *parent, not_null<Window::SessionController*> controller)
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

rpl::producer<QString> AyuPlugins::title() {
	return tr::ayu_JellyPlugins();
}

void AyuPlugins::refresh() {
	_content->clear();
	build(_content, [](SectionBuilder &builder) {
		const auto controller = builder.controller();
		const auto manager = QPointer<GummyPlugins::Manager>(&controller->session().plugins());
		builder.addSkip();
		builder.addButton({
			.id = u"jelly/plugins/install"_q,
			.title = tr::ayu_JellyInstall(),
			.icon = { &st::menuIconFile },
			.onClick = [=] {
				const auto path = QFileDialog::getOpenFileName(nullptr,
					tr::ayu_JellyInstall(tr::now), {}, u"GummyGram plugins (*.jellyplugin)"_q);
				if (path.isEmpty() || !manager) return;
				auto file = QFile(path);
				if (!file.open(QIODevice::ReadOnly) || file.size() > GummyPlugins::kPackageLimit) {
					controller->showToast(tr::ayu_JellyInvalidPackage(tr::now));
					return;
				}
				const auto data = file.read(GummyPlugins::kPackageLimit + 1);
				auto package = GummyPlugins::Package();
				auto error = QString();
				if (!GummyPlugins::ParsePackage(data, package, error)) {
					controller->showToast(error);
					return;
				}
				controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(tr::ayu_JellyInstall());
					AddLabel(box, package.name + u" · "_q + package.version + u"\n"_q + package.author);
					AddLabel(box, package.description);
					AddLabel(box, tr::ayu_JellyInstallNotice(tr::now));
					box->addButton(tr::ayu_JellyInstall(), [=] {
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
			.title = tr::ayu_JellyStopAll(),
			.icon = { &st::menuIconCancel },
			.onClick = [=] { if (manager) manager->stopAll(); },
		});
		builder.addSkip();
		builder.addDivider();
		builder.addSkip();
		builder.addSubsectionTitle(tr::ayu_JellyPlugins());
		const auto plugins = manager->plugins();
		if (plugins.empty()) {
			builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
				return { .widget = object_ptr<Ui::FlatLabel>(ctx.container,
					tr::ayu_JellyEmpty(), st::boxLabel) };
			});
		}
		for (const auto &info : plugins) {
			builder.addButton({
				.id = u"jelly/plugin/"_q + info.package.id,
				.title = rpl::single(info.package.name),
				.icon = { &st::menuIconBot },
				.label = rpl::single(info.enabled ? tr::ayu_JellyRunning(tr::now) : tr::ayu_JellyDisabled(tr::now)),
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
}

} // namespace Settings
