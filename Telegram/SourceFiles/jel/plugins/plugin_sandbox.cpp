#include "jel/plugins/plugin_sandbox.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#endif

namespace JellyPlugins {
namespace {

#ifdef Q_OS_WIN
struct Sandbox {
	PSID sid = nullptr;
	HANDLE job = nullptr;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
	std::vector<unsigned char> attributeMemory;
	STARTUPINFOEXW startup = {};
	SECURITY_CAPABILITIES capabilities = {};
	HANDLE handles[3] = {};

	~Sandbox() {
		if (attributes) {
			DeleteProcThreadAttributeList(attributes);
		}
		if (job) {
			CloseHandle(job);
		}
		if (sid) {
			FreeSid(sid);
		}
	}
};

bool GrantRuntimeRead(const QString &path) {
	auto packages = PSID(nullptr);
	if (!ConvertStringSidToSidW(L"S-1-15-2-1", &packages)) {
		return false;
	}
	auto descriptor = PSECURITY_DESCRIPTOR(nullptr);
	auto oldAcl = PACL(nullptr);
	const auto name = QDir::toNativeSeparators(path).toStdWString();
	auto status = GetNamedSecurityInfoW(
		const_cast<LPWSTR>(name.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, &oldAcl, nullptr, &descriptor);
	auto access = EXPLICIT_ACCESSW();
	access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
	access.grfAccessMode = GRANT_ACCESS;
	access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
	access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
	access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(packages);
	auto acl = PACL(nullptr);
	if (status == ERROR_SUCCESS) {
		status = SetEntriesInAclW(1, &access, oldAcl, &acl);
	}
	if (status == ERROR_SUCCESS) {
		status = SetNamedSecurityInfoW(
			const_cast<LPWSTR>(name.c_str()), SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
	}
	if (acl) {
		LocalFree(acl);
	}
	if (descriptor) {
		LocalFree(descriptor);
	}
	LocalFree(packages);
	return status == ERROR_SUCCESS;
}

QString StageRuntime(QString &error) {
	const auto executable = QCoreApplication::applicationFilePath();
	auto source = QFile(executable);
	if (!source.open(QIODevice::ReadOnly)) {
		error = u"Cannot read plugin runtime executable."_q;
		return {};
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	if (!hash.addData(&source)) {
		error = u"Cannot hash plugin runtime executable."_q;
		return {};
	}
	const auto folder = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
		+ u"/GummyGram/plugin-runtime/"_q
		+ QString::fromLatin1(hash.result().toHex());
	if (!QDir().mkpath(folder) || QFileInfo(folder).isSymLink()
		|| !GrantRuntimeRead(folder)) {
		error = u"Cannot prepare read-only AppContainer runtime directory."_q;
		return {};
	}
	const auto files = QDir(QCoreApplication::applicationDirPath()).entryList(
		{ QFileInfo(executable).fileName(), u"*.dll"_q }, QDir::Files);
	for (const auto &file : files) {
		const auto from = QCoreApplication::applicationDirPath() + '/' + file;
		const auto to = folder + '/' + file;
		if (QFileInfo(to).isSymLink()) {
			error = u"Runtime file must not be a symbolic link."_q;
			return {};
		}
		auto input = QFile(from);
		if (!input.open(QIODevice::ReadOnly)) {
			error = u"Cannot read runtime dependency."_q;
			return {};
		}
		auto output = QSaveFile(to);
		auto existing = QFile(to);
		if (existing.open(QIODevice::ReadOnly)) {
			auto existingHash = QCryptographicHash(QCryptographicHash::Sha256);
			auto sourceHash = QCryptographicHash(QCryptographicHash::Sha256);
			if (existingHash.addData(&existing) && sourceHash.addData(&input)
				&& existingHash.result() == sourceHash.result()) {
				continue;
			}
			if (!input.seek(0)) {
				error = u"Cannot rewind runtime dependency."_q;
				return {};
			}
		}
		if (!output.open(QIODevice::WriteOnly)) {
			error = u"Cannot stage plugin runtime."_q;
			return {};
		}
		while (!input.atEnd()) {
			const auto chunk = input.read(1024 * 1024);
			if (chunk.isEmpty() || output.write(chunk) != chunk.size()) {
				error = u"Cannot copy plugin runtime."_q;
				return {};
			}
		}
		if (!output.commit() || !GrantRuntimeRead(to)) {
			error = u"Cannot secure plugin runtime dependency."_q;
			return {};
		}
	}
	return folder + '/' + QFileInfo(executable).fileName();
}
#endif

} // namespace

std::shared_ptr<void> PrepareSandbox(QProcess &process, const QString &pluginId, QString &error) {
#ifdef Q_OS_WIN
	auto sandbox = std::make_shared<Sandbox>();
	const auto name = (u"GummyGram.Plugin."_q + QString::fromLatin1(
		QCryptographicHash::hash(pluginId.toUtf8(), QCryptographicHash::Sha256)
		.toHex().left(32))).toStdWString();
	const auto created = CreateAppContainerProfile(
		name.c_str(), name.c_str(), L"GummyGram isolated plugin", nullptr, 0, &sandbox->sid);
	if (FAILED(created)
		&& FAILED(DeriveAppContainerSidFromAppContainerName(name.c_str(), &sandbox->sid))) {
		error = u"AppContainer is unavailable; plugin execution is blocked."_q;
		return nullptr;
	}
	const auto executable = StageRuntime(error);
	if (executable.isEmpty()) {
		return nullptr;
	}
	sandbox->job = CreateJobObjectW(nullptr, nullptr);
	auto limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
		| JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
	limits.BasicLimitInformation.ActiveProcessLimit = 1;
	limits.ProcessMemoryLimit = 256 * 1024 * 1024;
	if (!sandbox->job || !SetInformationJobObject(
		sandbox->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
		error = u"Cannot establish plugin process limits."_q;
		return nullptr;
	}
	auto size = SIZE_T(0);
	InitializeProcThreadAttributeList(nullptr, 3, 0, &size);
	sandbox->attributeMemory.resize(size);
	sandbox->attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
		sandbox->attributeMemory.data());
	if (!InitializeProcThreadAttributeList(sandbox->attributes, 3, 0, &size)) {
		sandbox->attributes = nullptr;
		error = u"Cannot initialize plugin sandbox attributes."_q;
		return nullptr;
	}
	sandbox->capabilities.AppContainerSid = sandbox->sid;
	if (!UpdateProcThreadAttribute(sandbox->attributes, 0,
		PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &sandbox->capabilities,
		sizeof(sandbox->capabilities), nullptr, nullptr)
		|| !UpdateProcThreadAttribute(sandbox->attributes, 0,
			PROC_THREAD_ATTRIBUTE_JOB_LIST, &sandbox->job,
			sizeof(sandbox->job), nullptr, nullptr)) {
		error = u"Cannot configure AppContainer and job restrictions."_q;
		return nullptr;
	}
	process.setProgram(executable);
	process.setArguments({ u"--jelly-plugin-worker"_q });
	process.setWorkingDirectory(QFileInfo(executable).absolutePath());
	auto environment = QProcessEnvironment();
	environment.insert(u"SystemRoot"_q, qEnvironmentVariable("SystemRoot"));
	environment.insert(u"QV4_FORCE_INTERPRETER"_q, u"1"_q);
	process.setProcessEnvironment(environment);
	process.setCreateProcessArgumentsModifier([sandbox](QProcess::CreateProcessArguments *args) {
		sandbox->startup.StartupInfo = *args->startupInfo;
		sandbox->startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
		sandbox->startup.lpAttributeList = sandbox->attributes;
		sandbox->handles[0] = args->startupInfo->hStdInput;
		sandbox->handles[1] = args->startupInfo->hStdOutput;
		sandbox->handles[2] = args->startupInfo->hStdError;
		if (!UpdateProcThreadAttribute(sandbox->attributes, 0,
			PROC_THREAD_ATTRIBUTE_HANDLE_LIST, sandbox->handles,
			sizeof(sandbox->handles), nullptr, nullptr)) {
			args->applicationName = L"Z:\\GummyGram-invalid-sandbox\\no-worker.exe";
			args->arguments = const_cast<wchar_t*>(L"");
			return;
		}
		args->startupInfo = &sandbox->startup.StartupInfo;
		args->flags |= EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
		args->inheritHandles = true;
	});
	return sandbox;
#else
	error = u"This release requires Windows AppContainer. Plugins remain disabled on this platform."_q;
	return nullptr;
#endif
}

bool IsSandboxWorker() {
#ifdef Q_OS_WIN
	auto token = HANDLE(nullptr);
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return false;
	}
	auto isolated = DWORD(0);
	auto size = DWORD(0);
	const auto result = GetTokenInformation(
		token, TokenIsAppContainer, &isolated, sizeof(isolated), &size);
	CloseHandle(token);
	return result && isolated != 0;
#else
	return false;
#endif
}

} // namespace JellyPlugins
