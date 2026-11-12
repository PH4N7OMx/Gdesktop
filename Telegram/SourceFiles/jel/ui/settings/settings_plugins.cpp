#include "jel/ui/settings/settings_plugins.h"

#include "jel/plugins/plugin_service.h"
#include "jel/ui/settings/settings_main.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "lang_auto.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QPointer>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

using namespace Builder;
using JellyPlugins::ApprovalChoice;
using JellyPlugins::PendingAction;
using JellyPlugins::PluginInfo;
using JellyPlugins::Service;
using JellyPlugins::Client::AccessMode;
using JellyPlugins::Client::RuntimeState;

void Label(not_null<Ui::GenericBox*> box, const QString &text, bool secondary = false) {
	if (text.isEmpty()) return;
	const auto label = box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(text), secondary ? st::boxDividerLabel : st::boxLabel));
	label->setBreakEverywhere(true);
	label->setSelectable(true);
	box->addSkip(st::boxLittleSkip);
}

void Heading(not_null<Ui::GenericBox*> box, const PluginInfo &info) {
	box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(info.package.name), st::defaultSubsectionTitle));
	box->addSkip(st::boxLittleSkip);
	Label(box, info.package.version + (info.package.author.isEmpty() ? QString() : u" · "_q + info.package.author), true);
	Label(box, tr::jel_JellyV2LocalPackage(tr::now), true);
}

Ui::Checkbox *Check(not_null<Ui::GenericBox*> box, const QString &text, bool checked = false) {
	const auto result = box->addRow(object_ptr<Ui::Checkbox>(box, text, checked, st::defaultCheckbox));
	result->setAllowTextLines();
	result->setTextBreakEverywhere();
	box->addSkip(st::boxLittleSkip);
	return result;
}

void Row(not_null<Ui::GenericBox*> box, rpl::producer<QString> text, Fn<void()> callback) {
	box->addRow(object_ptr<Ui::SettingsButton>(box, std::move(text), st::settingsButton))->setClickedCallback(std::move(callback));
}

Ui::InputField *Text(not_null<Ui::GenericBox*> box, const QString &value, bool readOnly, bool code = false) {
	const auto field = box->addRow(object_ptr<Ui::InputField>(box, st::defaultInputField, Ui::InputField::Mode::MultiLine, rpl::single(QString()), value));
	field->setMinHeight(st::boxWideWidth / 2);
	field->setMaxHeight(st::boxWideWidth);
	field->rawTextEdit()->setReadOnly(readOnly);
	if (code) {
		auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		font.setPixelSize(st::boxLabel.style.font->f.pixelSize());
		auto format = QTextCharFormat();
		format.setFont(font);
		auto cursor = QTextCursor(field->rawTextEdit()->document());
		cursor.select(QTextCursor::Document);
		cursor.mergeCharFormat(format);
	}
	return field;
}

QString ModeName(AccessMode mode) {
	switch (mode) {
	case AccessMode::Inherit: return tr::jel_JellyV2ModeInherit(tr::now);
	case AccessMode::Ask: return tr::jel_JellyV2ModeAsk(tr::now);
	case AccessMode::Remember: return tr::jel_JellyV2ModeRemember(tr::now);
	case AccessMode::Full: return tr::jel_JellyV2ModeFull(tr::now);
	}
	Unexpected("Invalid plugin access mode");
}

QString Status(const PluginInfo &info) {
	if (info.waiting) return tr::jel_JellyV2Waiting(tr::now) + u" ("_q + QString::number(info.waiting) + ')';
	switch (info.state) {
	case RuntimeState::Starting: return tr::jel_JellyV2Starting(tr::now);
	case RuntimeState::Activating: return tr::jel_JellyV2Activating(tr::now);
	case RuntimeState::Ready: return tr::jel_JellyRunning(tr::now);
	case RuntimeState::Stopping: return tr::jel_JellyV2Stopping(tr::now);
	case RuntimeState::Failed: return tr::jel_JellyV2Failed(tr::now);
	case RuntimeState::Stopped: return tr::jel_JellyDisabled(tr::now);
	}
	Unexpected("Invalid plugin runtime state");
}

void ShowMode(not_null<Window::SessionController*> controller, const QString &id = {}, const QString &digest = {}) {
	const auto service = QPointer<Service>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(id.isEmpty() ? tr::jel_JellyV2GlobalMode() : tr::jel_JellyV2PluginMode());
		box->setWidth(st::boxWideWidth);
		Label(box, id.isEmpty() ? tr::jel_JellyV2GlobalNotice(tr::now) : tr::jel_JellyV2ModeNotice(tr::now));
		const auto guardedBox = QPointer<Ui::GenericBox>(box.get());
		if (service) {
			auto current = service->globalMode();
			for (const auto &info : service->plugins()) if (info.package.id == id) current = info.mode;
			Label(box, tr::jel_JellyV2PluginMode(tr::now) + u": "_q + ModeName(current), true);
		}
		for (const auto mode : { AccessMode::Inherit, AccessMode::Ask, AccessMode::Remember, AccessMode::Full }) {
			if (id.isEmpty() && mode == AccessMode::Inherit) continue;
			Row(box, rpl::single(ModeName(mode)), [=] {
				if (!service) return;
				const auto apply = [=] {
					if (!service) return;
					auto error = QString();
					if (id.isEmpty()) service->setGlobalMode(mode);
					else if (!service->setMode(id, digest, mode, error)) { controller->showToast(error); return; }
					if (guardedBox) guardedBox->closeBox();
				};
				if (mode != AccessMode::Full) { apply(); return; }
				controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> confirm) {
					confirm->setTitle(tr::jel_JellyV2ModeFull());
					confirm->setWidth(st::boxWideWidth);
					Label(confirm, tr::jel_JellyV2FullNotice(tr::now));
					if (id.isEmpty() && service) {
						auto count = 0;
						for (const auto &info : service->plugins()) if (info.mode == AccessMode::Inherit) ++count;
						Label(confirm, tr::jel_JellyV2Affected(tr::now) + QString::number(count));
						Label(confirm, tr::jel_JellyV2FuturePlugins(tr::now), true);
					}
					confirm->addButton(tr::lng_box_ok(), [=] { apply(); confirm->closeBox(); });
					confirm->addButton(tr::lng_cancel(), [=] { confirm->closeBox(); });
				}));
			});
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowAction(not_null<Window::SessionController*> controller, PendingAction pending) {
	const auto service = QPointer<Service>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		const auto guardedBox = QPointer<Ui::GenericBox>(box.get());
		box->setTitle(tr::jel_JellyV2ReviewAction());
		box->setWidth(st::boxWideWidth);
		Label(box, tr::jel_JellyV2ActualNotice(tr::now), true);
		Label(box, tr::jel_JellyV2RequestedBy(tr::now) + pending.pluginName, true);
		Label(box, tr::jel_JellyV2Account(tr::now) + pending.account);
		Label(box, pending.recipient);
		Label(box, u"Chat ID: "_q + pending.action.params.value(u"chatId"_q).toString(), true);
		const auto sending = pending.action.method == u"telegram.sendText"_q;
		if (sending) {
			Label(box, tr::jel_JellyV2SendText(tr::now));
			Text(box, pending.action.params.value(u"text"_q).toString(), true);
			Label(box, pending.action.params.value(u"silent"_q).toBool() ? tr::jel_JellyV2Silent(tr::now) : tr::jel_JellyV2Notify(tr::now), true);
			Label(box, tr::jel_JellyV2NoPreview(tr::now), true);
		} else Label(box, tr::jel_JellyV2ReadHistory(tr::now) + QString::number(pending.action.params.value(u"limit"_q).toInt()));
		const auto remember = Check(box, tr::jel_JellyV2RememberExact(tr::now));
		const auto broad = sending ? Check(box, tr::jel_JellyV2RememberChat(tr::now)) : nullptr;
		if (broad) {
			broad->setEnabled(remember->checked());
			remember->checkedChanges() | rpl::on_next([=](bool checked) { broad->setEnabled(checked); }, broad->lifetime());
		}
		Label(box, tr::jel_JellyV2RememberNotice(tr::now), true);
		const auto submit = [=](bool allow) {
			if (!service) return;
			const auto choice = allow
				? remember->checked() ? (broad && broad->checked() ? ApprovalChoice::RememberChat : ApprovalChoice::RememberExact) : ApprovalChoice::Once
				: remember->checked() ? ApprovalChoice::DenyAlways : ApprovalChoice::DenyOnce;
			auto error = QString();
			if (service->approve(pending.token, choice, error)) { if (guardedBox) guardedBox->closeBox(); }
			else controller->showToast(error);
		};
		box->addButton(tr::jel_JellyV2Allow(), [=] { submit(true); });
		box->addButton(tr::jel_JellyV2Deny(), [=] { submit(false); });
		service->changes() | rpl::on_next([=] {
			if (!service) return;
			for (const auto &current : service->pending()) if (current.token == pending.token) return;
			box->closeBox();
		}, box->lifetime());
	}));
}

void ShowPending(not_null<Window::SessionController*> controller) {
	const auto service = QPointer<Service>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::jel_JellyV2Pending());
		box->setWidth(st::boxWideWidth);
		if (!service) return;
		const auto pending = service->pending();
		if (pending.empty()) Label(box, tr::jel_JellyV2NoPending(tr::now));
		for (const auto &action : pending) Row(box, rpl::single(action.pluginName + u" · "_q + action.action.method), [=] { ShowAction(controller, action); });
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowRules(not_null<Window::SessionController*> controller, PluginInfo info) {
	const auto service = QPointer<Service>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::jel_JellyV2Rules());
		box->setWidth(st::boxWideWidth);
		if (info.rules.isEmpty()) Label(box, tr::jel_JellyV2NoRules(tr::now));
		for (const auto &item : info.rules) {
			const auto rule = item.toObject();
			Label(box, (rule.value(u"allow"_q).toBool() ? tr::jel_JellyV2Allow(tr::now) : tr::jel_JellyV2Deny(tr::now)) + u" · "_q + rule.value(u"method"_q).toString());
			if (rule.value(u"scope"_q).toString() == u"chat-text"_q) {
				Label(box, tr::jel_JellyV2RememberChat(tr::now));
				Label(box, u"Chat ID: "_q + rule.value(u"chatId"_q).toString(), true);
				Label(box, rule.value(u"silent"_q).toBool() ? tr::jel_JellyV2Silent(tr::now) : tr::jel_JellyV2Notify(tr::now), true);
			} else Text(box, QString::fromUtf8(QJsonDocument(rule.value(u"params"_q).toObject()).toJson()), true, true);
			Row(box, tr::lng_box_delete(), [=] {
				if (!service) return;
				auto error = QString();
				if (service->removeRule(info.package.id, info.package.digest, rule, error)) box->closeBox();
				else controller->showToast(error);
			});
		}
		if (!info.rules.isEmpty()) Row(box, tr::jel_JellyV2ClearRules(), [=] {
			if (!service) return;
			auto error = QString();
			if (service->clearRules(info.package.id, info.package.digest, error)) box->closeBox();
			else controller->showToast(error);
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowSettings(not_null<Window::SessionController*> controller, PluginInfo info) {
	const auto service = QPointer<Service>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::jel_JellyConfiguration());
		box->setWidth(st::boxWideWidth);
		Label(box, tr::jel_JellyV2SettingsNotice(tr::now));
		const auto field = Text(box, QString::fromUtf8(QJsonDocument(info.settings).toJson()), false, true);
		box->addButton(tr::lng_settings_save(), [=] {
			if (!service) return;
			const auto document = QJsonDocument::fromJson(field->getLastText().toUtf8());
			if (!document.isObject()) { field->showError(); return; }
			auto error = QString();
			if (service->configure(info.package.id, info.package.digest, document.object(), error)) box->closeBox();
			else controller->showToast(error);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowDetails(not_null<Window::SessionController*> controller, PluginInfo info) {
	const auto service = QPointer<Service>(&controller->session().plugins());
	controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
		const auto guardedBox = QPointer<Ui::GenericBox>(box.get());
		box->setTitle(tr::jel_JellyPlugins());
		box->setWidth(st::boxWideWidth);
		Heading(box, info);
		Label(box, Status(info));
		Label(box, info.status);
		Label(box, info.package.description, true);
		Label(box, tr::jel_JellyV2AppOpen(tr::now), true);
		if (info.nextRun) Label(box, tr::jel_JellyV2NextRun(tr::now) + QDateTime::fromMSecsSinceEpoch(info.nextRun).toLocalTime().toString(u"dd.MM HH:mm:ss"_q));
		Row(box, rpl::single(tr::jel_JellyV2PluginMode(tr::now) + u": "_q + ModeName(info.mode)), [=] { ShowMode(controller, info.package.id, info.package.digest); });
		Row(box, tr::jel_JellyV2Rules(), [=] { ShowRules(controller, info); });
		Row(box, tr::jel_JellyConfiguration(), [=] { ShowSettings(controller, info); });
		Row(box, tr::jel_JellyViewCode(), [=] {
			controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> codeBox) {
				codeBox->setTitle(tr::jel_JellyViewCode());
				codeBox->setWidth(2 * st::boxWideWidth);
				Label(codeBox, u"SHA-256: "_q + info.package.digest, true);
				Text(codeBox, info.package.code, true, true);
				codeBox->addButton(tr::lng_close(), [=] { codeBox->closeBox(); });
			}));
		});
		Row(box, tr::jel_JellyLog(), [=] {
			controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> logBox) {
				logBox->setTitle(tr::jel_JellyLog());
				logBox->setWidth(2 * st::boxWideWidth);
				if (service) for (const auto &current : service->plugins()) if (current.package.id == info.package.id) Text(logBox, current.log.join('\n'), true, true);
				logBox->addButton(tr::lng_close(), [=] { logBox->closeBox(); });
			}));
		});
		if (info.canRollback) Row(box, tr::jel_JellyV2Rollback(), [=] {
			controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> confirm) {
				confirm->setTitle(tr::jel_JellyV2Rollback());
				confirm->setWidth(st::boxWideWidth);
				Label(confirm, tr::jel_JellyV2RollbackNotice(tr::now));
				confirm->addButton(tr::jel_JellyV2Rollback(), [=] {
					if (!service) return;
					auto error = QString();
					if (service->rollback(info.package.id, info.package.digest, error)) { confirm->closeBox(); if (guardedBox) guardedBox->closeBox(); }
					else controller->showToast(error);
				});
				confirm->addButton(tr::lng_cancel(), [=] { confirm->closeBox(); });
			}));
		});
		Row(box, tr::jel_JellyRemove(), [=] {
			controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> confirm) {
				confirm->setTitle(tr::jel_JellyRemove());
				confirm->setWidth(st::boxWideWidth);
				const auto removeData = Check(confirm, tr::jel_JellyV2RemoveData(tr::now), true);
				confirm->addButton(tr::jel_JellyRemove(), [=] {
					if (!service) return;
					auto error = QString();
					if (service->uninstall(info.package.id, info.package.digest, removeData->checked(), error)) { confirm->closeBox(); if (guardedBox) guardedBox->closeBox(); }
					else controller->showToast(error);
				});
				confirm->addButton(tr::lng_cancel(), [=] { confirm->closeBox(); });
			}));
		});
		box->addButton(info.enabled ? tr::jel_JellyV2Disable() : tr::jel_JellyEnable(), [=] {
			if (!service) return;
			auto error = QString();
			const auto success = info.enabled ? service->disable(info.package.id, error) : service->enable(info.package.id, info.package.digest, error);
			if (success) box->closeBox();
			else controller->showToast(error);
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

QMargins ListMargins() {
	return QMargins(st::settingsCheckboxPadding.left(), 0, st::settingsCheckboxPadding.left(), st::settingsCheckboxPadding.bottom());
}

const auto kMeta = BuildSectionMeta({
	.id = JelPlugins::Id(), .parentId = JelMain::Id(), .title = &tr::jel_JellyPlugins, .icon = &st::menuIconBot,
}, [](SectionBuilder &builder) {
	const auto controller = builder.controller();
	if (!controller) return;
	const auto service = QPointer<Service>(&controller->session().plugins());
	builder.addSkip();
	builder.addButton({ .id = u"jelly/v2/install"_q, .title = tr::jel_JellyInstall(), .icon = { &st::menuIconFile }, .onClick = [=] {
		const auto path = QFileDialog::getOpenFileName(nullptr, tr::jel_JellyInstall(tr::now), {}, u"JellyPlugins (*.jelly)"_q);
		if (path.isEmpty() || !service) return;
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly) || file.size() > JellyPlugins::Client::Limit("packageBytes")) { controller->showToast(tr::jel_JellyInvalidPackage(tr::now)); return; }
		const auto data = file.read(JellyPlugins::Client::Limit("packageBytes") + 1);
		auto package = JellyPlugins::Client::Package();
		auto error = QString();
		if (!JellyPlugins::Client::ParsePackage(data, package, error)) { controller->showToast(error); return; }
		controller->show(Box<Ui::GenericBox>([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(tr::jel_JellyInstall());
			box->setWidth(st::boxWideWidth);
			auto info = PluginInfo();
			info.package = package;
			Heading(box, info);
			Label(box, package.description);
			Label(box, tr::jel_JellyInstallNotice(tr::now), true);
			box->addButton(tr::jel_JellyInstall(), [=] {
				if (!service) return;
				auto error = QString();
				if (service->install(data, error)) box->closeBox();
				else controller->showToast(error);
			});
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}));
	} });
	builder.addButton({ .id = u"jelly/v2/mode"_q, .title = tr::jel_JellyV2GlobalMode(), .icon = { &st::menuIconPermissions },
		.label = rpl::single(ModeName(service->globalMode())), .onClick = [=] { ShowMode(controller); } });
	builder.addButton({ .id = u"jelly/v2/pending"_q, .title = tr::jel_JellyV2Pending(), .icon = { &st::menuIconInfo },
		.label = rpl::single(QString::number(service->pending().size())), .onClick = [=] { ShowPending(controller); } });
	builder.addButton({ .id = u"jelly/v2/stop"_q, .title = tr::jel_JellyStopAll(), .icon = { &st::menuIconCancel }, .onClick = [=] { if (service) service->stopAll(); } });
	builder.addButton({ .id = u"jelly/v2/docs"_q, .title = tr::jel_JellyDocumentation(), .icon = { &st::menuIconInfo }, .onClick = [] {
		QDesktopServices::openUrl(QUrl(u"https://jellygram.gitbook.io/jelly-plugins/"_q));
	} });
	builder.addSkip();
	builder.addDivider();
	builder.addSubsectionTitle(tr::jel_JellyPlugins());
	for (const auto &error : service->diagnostics()) builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return { .widget = object_ptr<Ui::FlatLabel>(ctx.container, rpl::single(error), st::boxLabel), .margin = ListMargins() };
	});
	for (const auto &info : service->plugins()) builder.addButton({ .id = u"jelly/v2/plugin/"_q + info.package.id,
		.title = rpl::single(info.package.name), .icon = { &st::menuIconBot }, .label = rpl::single(Status(info) + u" · "_q + ModeName(info.mode)),
		.onClick = [=] { ShowDetails(controller, info); } });
	if (service->plugins().empty()) builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return { .widget = object_ptr<Ui::FlatLabel>(ctx.container, tr::jel_JellyEmpty(), st::boxLabel), .margin = ListMargins() };
	});
});

} // namespace

JelPlugins::JelPlugins(QWidget *parent, not_null<Window::SessionController*> controller) : Section(parent, controller) {
	_content = Ui::CreateChild<Ui::VerticalLayout>(this);
	refresh();
	Ui::ResizeFitChild(this, _content);
	controller->session().plugins().changes() | rpl::on_next([=] {
		if (_refreshPending) return;
		_refreshPending = true;
		QTimer::singleShot(0, this, [=] { _refreshPending = false; refresh(); });
	}, lifetime());
}

rpl::producer<QString> JelPlugins::title() { return tr::jel_JellyPlugins(); }
void JelPlugins::refresh() { _content->clear(); build(_content, kMeta.build); }

} // namespace Settings
