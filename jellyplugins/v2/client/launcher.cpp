#include "launcher.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>

#include <vector>
#include <bit>

#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#include <userenv.h>
#endif

namespace JellyPlugins::Client {
namespace {

#ifdef Q_OS_WIN
struct Sandbox {
	HMODULE userenv = nullptr;
	PSID sid = nullptr;
	HANDLE job = nullptr;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
	std::vector<unsigned char> memory;
	STARTUPINFOEXW startup = {};
	SECURITY_CAPABILITIES capabilities = {};
	HANDLE pipes[3] = {};

	~Sandbox() {
		if (attributes) DeleteProcThreadAttributeList(attributes);
		if (job) CloseHandle(job);
		if (sid) FreeSid(sid);
		if (userenv) FreeLibrary(userenv);
	}
};

void GrantRead(const QString &path, PSID sid) {
	const auto name = QDir::toNativeSeparators(path).toStdWString();
	auto descriptor = PSECURITY_DESCRIPTOR(nullptr);
	auto previous = PACL(nullptr);
	if (GetNamedSecurityInfoW(name.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, &previous, nullptr, &descriptor) != ERROR_SUCCESS) return;
	auto access = EXPLICIT_ACCESSW();
	access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
	access.grfAccessMode = GRANT_ACCESS;
	access.grfInheritance = NO_INHERITANCE;
	access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
	access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
	auto acl = PACL(nullptr);
	if (SetEntriesInAclW(1, &access, previous, &acl) == ERROR_SUCCESS) {
		SetNamedSecurityInfoW(const_cast<wchar_t*>(name.c_str()), SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
	}
	if (acl) LocalFree(acl);
	LocalFree(descriptor);
}
#endif

} // namespace

QString HostPath() {
	const auto folder = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MAC
	return QDir::cleanPath(folder + QStringLiteral("/../Helpers/jelly-plugin-host"));
#else
#ifdef Q_OS_WIN
	const auto name = QStringLiteral("jelly-plugin-host.exe");
#else
	const auto name = QStringLiteral("jelly-plugin-host");
#endif
	const auto adjacent = folder + QStringLiteral("/jelly-runtime/") + name;
#ifndef Q_OS_WIN
	const auto relative = QDir::cleanPath(folder + QStringLiteral("/../libexec/gummygram/") + name);
	if (!QFileInfo::exists(adjacent) && QFileInfo::exists(relative)) return relative;
#endif
#ifdef JELLY_INSTALLED_HOST_PATH
	if (!QFileInfo::exists(adjacent)) return QString::fromUtf8(JELLY_INSTALLED_HOST_PATH);
#endif
	return adjacent;
#endif
}

std::shared_ptr<void> PrepareLaunch(QProcess &process, const QString &path, const QString &isolationKey, QString &error) {
	if (isolationKey.isEmpty()) { error = QStringLiteral("Missing plugin isolation key."); return nullptr; }
	const auto file = QFileInfo(path);
	if (!file.isFile() || file.isSymLink() || !file.isExecutable()) {
		error = QStringLiteral("Plugin host is missing or not executable: ") + path;
		return nullptr;
	}
	process.setProgram(file.absoluteFilePath());
	process.setArguments({});
	process.setWorkingDirectory(file.absolutePath());
	auto environment = QProcessEnvironment();
#ifdef Q_OS_WIN
	auto sandbox = std::make_shared<Sandbox>();
	sandbox->userenv = LoadLibraryExW(L"userenv.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!sandbox->userenv) { error = QStringLiteral("AppContainer is unavailable on this Windows version."); return nullptr; }
	const auto create = std::bit_cast<decltype(&CreateAppContainerProfile)>(GetProcAddress(sandbox->userenv, "CreateAppContainerProfile"));
	const auto derive = std::bit_cast<decltype(&DeriveAppContainerSidFromAppContainerName)>(GetProcAddress(sandbox->userenv, "DeriveAppContainerSidFromAppContainerName"));
	if (!create || !derive) { error = QStringLiteral("AppContainer is unavailable on this Windows version."); return nullptr; }
	const auto profile = (QStringLiteral("GummyGram.JellyV2.") + QString::fromLatin1(
		QCryptographicHash::hash(isolationKey.toUtf8(), QCryptographicHash::Sha256).toHex().left(32))).toStdWString();
	if (FAILED(create(profile.c_str(), profile.c_str(), L"JellyPlugins v2", nullptr, 0, &sandbox->sid))
		&& FAILED(derive(profile.c_str(), &sandbox->sid))) {
		error = QStringLiteral("Cannot create an AppContainer profile.");
		return nullptr;
	}
	GrantRead(file.absolutePath(), sandbox->sid);
	GrantRead(file.absoluteFilePath(), sandbox->sid);
	sandbox->job = CreateJobObjectW(nullptr, nullptr);
	auto limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
		| JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
	limits.BasicLimitInformation.ActiveProcessLimit = 1;
	limits.ProcessMemoryLimit = 256 * 1024 * 1024;
	if (!sandbox->job || !SetInformationJobObject(sandbox->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
		error = QStringLiteral("Cannot restrict the plugin Job Object.");
		return nullptr;
	}
	auto size = SIZE_T(0);
	InitializeProcThreadAttributeList(nullptr, 3, 0, &size);
	sandbox->memory.resize(size);
	sandbox->attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(sandbox->memory.data());
	if (!InitializeProcThreadAttributeList(sandbox->attributes, 3, 0, &size)) {
		sandbox->attributes = nullptr;
		error = QStringLiteral("Cannot prepare AppContainer attributes.");
		return nullptr;
	}
	sandbox->capabilities.AppContainerSid = sandbox->sid;
	if (!UpdateProcThreadAttribute(sandbox->attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
		&sandbox->capabilities, sizeof(sandbox->capabilities), nullptr, nullptr)
		|| !UpdateProcThreadAttribute(sandbox->attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
			&sandbox->job, sizeof(sandbox->job), nullptr, nullptr)) {
		error = QStringLiteral("Cannot attach plugin process restrictions.");
		return nullptr;
	}
	environment.insert(QStringLiteral("SystemRoot"), qEnvironmentVariable("SystemRoot"));
	environment.insert(QStringLiteral("LOCALAPPDATA"), QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
	process.setCreateProcessArgumentsModifier([sandbox](QProcess::CreateProcessArguments *args) {
		sandbox->startup.StartupInfo = *args->startupInfo;
		sandbox->startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
		sandbox->startup.lpAttributeList = sandbox->attributes;
		sandbox->pipes[0] = args->startupInfo->hStdInput;
		sandbox->pipes[1] = args->startupInfo->hStdOutput;
		sandbox->pipes[2] = args->startupInfo->hStdError;
		if (!UpdateProcThreadAttribute(sandbox->attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
			sandbox->pipes, sizeof(sandbox->pipes), nullptr, nullptr)) {
			args->applicationName = L"Z:\\JellyPlugins-invalid-launch\\host.exe";
			args->arguments = const_cast<wchar_t*>(L"");
			return;
		}
		args->startupInfo = &sandbox->startup.StartupInfo;
		args->flags |= EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
		args->inheritHandles = true;
	});
	process.setProcessEnvironment(environment);
	return sandbox;
#else
	environment.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
	environment.insert(QStringLiteral("TZ"), QStringLiteral("UTC"));
#ifdef Q_OS_MAC
	environment.insert(QStringLiteral("HOME"), QDir::homePath());
	environment.insert(QStringLiteral("TMPDIR"), QDir::tempPath());
#endif
	process.setProcessEnvironment(environment);
	return std::make_shared<int>(0);
#endif
}

} // namespace JellyPlugins::Client
