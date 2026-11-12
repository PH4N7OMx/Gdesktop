#include "sandbox.h"

#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <dirent.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace JellyPlugins::Host {
namespace {

#if defined(__linux__) || defined(__APPLE__)
bool LimitResources() {
	auto core = rlimit{ 0, 0 };
	return setrlimit(RLIMIT_CORE, &core) == 0;
}
#endif

#ifdef __linux__
bool CloseDescriptors() {
	const auto directory = opendir("/proc/self/fd");
	if (!directory) {
		return false;
	}
	const auto own = dirfd(directory);
	while (const auto entry = readdir(directory)) {
		auto end = static_cast<char*>(nullptr);
		const auto descriptor = std::strtol(entry->d_name, &end, 10);
		if (end != entry->d_name && !*end && descriptor > 2 && descriptor != own) {
			close(int(descriptor));
		}
	}
	return closedir(directory) == 0;
}

bool InstallFilter() {
#if defined(__x86_64__)
	constexpr auto architecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
	constexpr auto architecture = AUDIT_ARCH_AARCH64;
#else
	return false;
#endif
#if defined(__x86_64__) || defined(__aarch64__)
	auto filter = std::vector<sock_filter>{
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
	};
	const auto allow = [&](uint32_t number) {
		filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 1));
		filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
	};
	const auto allowPipe = [&](uint32_t number) {
		filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 4));
		filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0])));
		filter.push_back(BPF_JUMP(BPF_JMP | BPF_JGT | BPF_K, 2, 0, 1));
		filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
		filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
	};
	allowPipe(__NR_read);
	allowPipe(__NR_write);
	allowPipe(__NR_fstat);
	allow(__NR_close);
	allow(__NR_brk);
	allow(__NR_munmap);
	allow(__NR_mremap);
	allow(__NR_madvise);
	allow(__NR_clock_gettime);
	allow(__NR_gettimeofday);
	allow(__NR_getrusage);
	allow(__NR_getpid);
	allow(__NR_gettid);
	allow(__NR_futex);
	allow(__NR_rt_sigaction);
	allow(__NR_rt_sigprocmask);
	allow(__NR_rt_sigreturn);
	allow(__NR_sigaltstack);
	allow(__NR_exit);
	allow(__NR_exit_group);
	filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mmap, 0, 7));
	filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[2])));
	filter.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, PROT_EXEC, 4, 0));
	filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[3])));
	filter.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, MAP_ANONYMOUS, 0, 2));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
	filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mprotect, 0, 4));
	filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[2])));
	filter.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, PROT_EXEC, 0, 1));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
	auto program = sock_fprog{ static_cast<unsigned short>(filter.size()), filter.data() };
	return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0
		&& prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
#endif
}
#endif

} // namespace

bool EnterSandbox(std::string &mechanism, std::string &error) {
#ifdef _WIN32
	auto token = HANDLE(nullptr);
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		error = "Cannot inspect process token";
		return false;
	}
	auto isolated = DWORD(0);
	auto size = DWORD(0);
	const auto appContainer = GetTokenInformation(token, TokenIsAppContainer, &isolated, sizeof(isolated), &size);
	auto capabilityBytes = std::vector<unsigned char>(4096);
	const auto inspected = GetTokenInformation(token, TokenCapabilities, capabilityBytes.data(), DWORD(capabilityBytes.size()), &size);
	const auto capabilities = reinterpret_cast<const TOKEN_GROUPS*>(capabilityBytes.data());
	CloseHandle(token);
	auto inJob = BOOL(FALSE);
	auto limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
	const auto required = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
		| JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
	if (!appContainer || !isolated || !inspected || capabilities->GroupCount != 0
		|| !IsProcessInJob(GetCurrentProcess(), nullptr, &inJob) || !inJob
		|| !QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr)
		|| (limits.BasicLimitInformation.LimitFlags & required) != required
		|| limits.BasicLimitInformation.ActiveProcessLimit != 1
		|| limits.ProcessMemoryLimit > 256 * 1024 * 1024) {
		error = "Host requires a capability-free AppContainer and a restricted Job Object";
		return false;
	}
	mechanism = "windows-appcontainer";
	return true;
#elif defined(__linux__)
	auto memory = rlimit{ 256 * 1024 * 1024, 256 * 1024 * 1024 };
	if (!LimitResources() || setrlimit(RLIMIT_AS, &memory) != 0
		|| !CloseDescriptors() || !InstallFilter()) {
		error = "Cannot install Linux resource limits and seccomp filter (x86_64/aarch64 with /proc required)";
		return false;
	}
	mechanism = "linux-seccomp";
	return true;
#elif defined(__APPLE__)
	auto code = SecCodeRef(nullptr);
	auto staticCode = SecStaticCodeRef(nullptr);
	auto information = CFDictionaryRef(nullptr);
	auto valid = SecCodeCopySelf(kSecCSDefaultFlags, &code) == errSecSuccess;
	if (valid) valid = SecCodeCheckValidity(code, kSecCSStrictValidate, nullptr) == errSecSuccess;
	if (valid) valid = SecCodeCopyStaticCode(code, kSecCSDefaultFlags, &staticCode) == errSecSuccess;
	if (valid) valid = SecCodeCopySigningInformation(staticCode, kSecCSSigningInformation, &information) == errSecSuccess;
	if (valid) {
		const auto entitlements = static_cast<CFDictionaryRef>(CFDictionaryGetValue(information, kSecCodeInfoEntitlementsDict));
		valid = entitlements && CFGetTypeID(entitlements) == CFDictionaryGetTypeID()
			&& CFDictionaryGetValue(entitlements, CFSTR("com.apple.security.app-sandbox")) == kCFBooleanTrue
			&& CFDictionaryGetCount(entitlements) == 1;
	}
	if (information) CFRelease(information);
	if (staticCode) CFRelease(staticCode);
	if (code) CFRelease(code);
	if (!valid || !LimitResources()) {
		error = "Host requires a valid signature and only its own App Sandbox entitlement; inheritance is forbidden";
		return false;
	}
	mechanism = "macos-app-sandbox";
	return true;
#else
	error = "Unsupported host platform";
	return false;
#endif
}

} // namespace JellyPlugins::Host
