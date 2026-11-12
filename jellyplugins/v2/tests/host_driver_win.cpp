#include <windows.h>
#include <aclapi.h>
#include <userenv.h>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Resources {
	PSID sid = nullptr;
	HANDLE job = nullptr;
	PROCESS_INFORMATION process = {};
	std::array<HANDLE, 3> pipes = {};
	std::vector<unsigned char> attributeMemory;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
	std::wstring profile;

	~Resources() {
		if (process.hThread) CloseHandle(process.hThread);
		if (process.hProcess) CloseHandle(process.hProcess);
		if (job) CloseHandle(job);
		for (const auto pipe : pipes) if (pipe) CloseHandle(pipe);
		if (attributes) DeleteProcThreadAttributeList(attributes);
		if (sid) FreeSid(sid);
		if (!profile.empty()) DeleteAppContainerProfile(profile.c_str());
	}

};

bool GrantRead(const std::wstring &path, PSID sid) {
	auto descriptor = PSECURITY_DESCRIPTOR(nullptr);
	auto previous = PACL(nullptr);
	if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, &previous, nullptr, &descriptor) != ERROR_SUCCESS) return false;
	auto access = EXPLICIT_ACCESSW();
	access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
	access.grfAccessMode = GRANT_ACCESS;
	access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
	access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
	access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
	auto acl = PACL(nullptr);
	const auto success = SetEntriesInAclW(1, &access, previous, &acl) == ERROR_SUCCESS
		&& SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr) == ERROR_SUCCESS;
	if (acl) LocalFree(acl);
	LocalFree(descriptor);
	return success;
}

std::wstring Quote(const std::wstring &value) {
	auto result = std::wstring(L"\"");
	auto slashes = size_t(0);
	for (const auto character : value) {
		if (character == L'\\') {
			++slashes;
		} else {
			result.append(slashes * (character == L'"' ? 2 : 1), L'\\');
			if (character == L'"') result += L'\\';
			result += character;
			slashes = 0;
		}
	}
	result.append(slashes * 2, L'\\');
	return result + L'"';
}

std::wstring Environment(const wchar_t *name) {
	const auto size = GetEnvironmentVariableW(name, nullptr, 0);
	if (!size) return {};
	auto value = std::wstring(size, L'\0');
	GetEnvironmentVariableW(name, value.data(), size);
	value.resize(size - 1);
	return value;
}

int Run(int argc, wchar_t **argv) {
	if (argc < 2) return 64;
	auto resources = Resources();
	const auto profile = L"GummyGram.JellyV2.Test." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
	if (FAILED(CreateAppContainerProfile(profile.c_str(), profile.c_str(),
		L"JellyPlugins isolated test", nullptr, 0, &resources.sid))) return 77;
	resources.profile = profile;
	auto executable = std::wstring(32768, L'\0');
	const auto length = GetFullPathNameW(argv[1], DWORD(executable.size()), executable.data(), nullptr);
	if (!length || length >= executable.size()) return 64;
	executable.resize(length);
	const auto folder = executable.substr(0, executable.find_last_of(L"\\/"));
	if (!GrantRead(folder, resources.sid) || !GrantRead(executable, resources.sid)) return 77;
	resources.job = CreateJobObjectW(nullptr, nullptr);
	auto limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
		| JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
	limits.BasicLimitInformation.ActiveProcessLimit = 1;
	limits.ProcessMemoryLimit = 256 * 1024 * 1024;
	if (!resources.job || !SetInformationJobObject(resources.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return 77;
	auto size = SIZE_T(0);
	InitializeProcThreadAttributeList(nullptr, 3, 0, &size);
	resources.attributeMemory.resize(size);
	resources.attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(resources.attributeMemory.data());
	if (!InitializeProcThreadAttributeList(resources.attributes, 3, 0, &size)) {
		resources.attributes = nullptr;
		return 77;
	}
	const auto streams = std::array<DWORD, 3>{ STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
	for (auto i = size_t(0); i != streams.size(); ++i) {
		if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(streams[i]), GetCurrentProcess(),
			&resources.pipes[i], 0, TRUE, DUPLICATE_SAME_ACCESS)) return 77;
	}
	auto capabilities = SECURITY_CAPABILITIES();
	capabilities.AppContainerSid = resources.sid;
	if (!UpdateProcThreadAttribute(resources.attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
		&capabilities, sizeof(capabilities), nullptr, nullptr)
		|| !UpdateProcThreadAttribute(resources.attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
			&resources.job, sizeof(resources.job), nullptr, nullptr)
		|| !UpdateProcThreadAttribute(resources.attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
			resources.pipes.data(), sizeof(resources.pipes), nullptr, nullptr)) return 77;
	auto startup = STARTUPINFOEXW();
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = resources.pipes[0];
	startup.StartupInfo.hStdOutput = resources.pipes[1];
	startup.StartupInfo.hStdError = resources.pipes[2];
	startup.lpAttributeList = resources.attributes;
	auto command = Quote(executable);
	for (auto i = 2; i < argc; ++i) command += L" " + Quote(argv[i]);
	auto environment = L"LOCALAPPDATA=" + Environment(L"LOCALAPPDATA") + L'\0'
		+ L"SystemRoot=" + Environment(L"SystemRoot") + L'\0' + L'\0';
	if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
		EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
		environment.data(), folder.c_str(), &startup.StartupInfo, &resources.process)) {
		std::fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
		return 77;
	}
	if (WaitForSingleObject(resources.process.hProcess, 60000) != WAIT_OBJECT_0) {
		TerminateJobObject(resources.job, 72);
		return 72;
	}
	auto exitCode = DWORD(0);
	return GetExitCodeProcess(resources.process.hProcess, &exitCode) ? int(exitCode) : 70;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
	return Run(argc, argv);
}
