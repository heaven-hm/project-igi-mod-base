// IGI-Injector: 32-bit x86 DLL injector for the IGI 1 internal menu demo.
//
// It loads the DLL with a remote LoadLibraryW call and reports the module base
// the loader returned. It never writes game memory beyond the argument buffer
// it allocates for the DLL path.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <optional>
#include <string>
#include <vector>
#include <wchar.h>

namespace {
struct Options {
    DWORD pid = 0;
    std::string processName = "igi.exe";
    std::string dllPath;
    bool list = false;
    bool verbose = false;
};

void usage() {
    std::printf(
        "IGI-Injector (x86)\n"
        "Usage: IGI-Injector [-p pid | -n name] -i <dll> [-v]\n"
        "       IGI-Injector -l [name]\n"
        "\n"
        "  -p <pid>    attach to a process id\n"
        "  -n <name>   match by executable name (default igi.exe)\n"
        "  -i <dll>    absolute path to the 32-bit DLL to load\n"
        "  -l          list matching processes and exit\n"
        "  -v          verbose output\n"
        "  -h          show this help\n");
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, out.data(), size);
    out.resize(std::wcslen(out.c_str()));
    return out;
}

std::string toNarrow(const wchar_t* text) {
    if (!text || !*text) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<std::size_t>(size - 1));
    return out;
}

struct ProcessInfo {
    DWORD pid = 0;
    std::wstring name;
};

std::vector<ProcessInfo> snapshotProcesses() {
    std::vector<ProcessInfo> processes;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return processes;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        processes.push_back({entry.th32ProcessID, entry.szExeFile});
    }
    CloseHandle(snapshot);
    return processes;
}

std::optional<DWORD> findLoadedModule(DWORD pid, const std::wstring& dllPath) {
    const HANDLE snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Module32FirstW(snapshot, &entry); more; more = Module32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExePath, dllPath.c_str()) == 0) {
            const DWORD base = reinterpret_cast<DWORD>(entry.modBaseAddr);
            CloseHandle(snapshot);
            return base;
        }
    }
    CloseHandle(snapshot);
    return 0;
}

int listProcesses(const std::string& filter) {
    const std::wstring wanted = toWide(filter);
    int matches = 0;
    for (const auto& process : snapshotProcesses()) {
        if (!wanted.empty() && _wcsicmp(process.name.c_str(), wanted.c_str()) != 0) continue;
        std::printf("%6lu  %s\n", static_cast<unsigned long>(process.pid), toNarrow(process.name.c_str()).c_str());
        ++matches;
    }
    if (matches == 0) std::printf("no process matched '%s'\n", filter.c_str());
    return matches == 0 ? 3 : 0;
}

int injectInto(DWORD pid, const std::string& dllPath, bool verbose) {
    const std::wstring widePath = toWide(dllPath);
    if (widePath.empty() || widePath.size() < 3) {
        std::printf("error: the DLL path must be absolute\n");
        return 2;
    }

    const auto existingModule = findLoadedModule(pid, widePath);
    if (!existingModule.has_value()) {
        std::printf("error: unable to inspect modules for pid %lu: %lu\n",
                    static_cast<unsigned long>(pid), GetLastError());
        return 4;
    }
    if (*existingModule != 0) {
        std::printf("already loaded in pid %lu (module 0x%08lX); restart igi.exe to reload %s\n",
                    static_cast<unsigned long>(pid), static_cast<unsigned long>(*existingModule),
                    dllPath.c_str());
        return 0;
    }

    const HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE,
        FALSE, pid);
    if (!process) {
        std::printf("error: OpenProcess(%lu) failed: %lu\n",
                    static_cast<unsigned long>(pid), GetLastError());
        return 4;
    }

    BOOL targetIsWow64 = FALSE;
    BOOL selfIsWow64 = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &selfIsWow64) && selfIsWow64 &&
        IsWow64Process(process, &targetIsWow64) && !targetIsWow64) {
        std::printf("error: pid %lu is not a 32-bit process; this injector is x86\n",
                    static_cast<unsigned long>(pid));
        CloseHandle(process);
        return 4;
    }

    const SIZE_T bytes = (widePath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::printf("error: VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(process);
        return 5;
    }
    if (!WriteProcessMemory(process, remote, widePath.c_str(), bytes, nullptr)) {
        std::printf("error: WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 6;
    }

    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    const HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) {
        std::printf("error: CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 7;
    }

    const DWORD waitResult = WaitForSingleObject(thread, 15000);
    if (waitResult != WAIT_OBJECT_0) {
        std::printf("error: remote loader did not finish (wait=%lu); allocation retained\n", static_cast<unsigned long>(waitResult));
        CloseHandle(thread);
        CloseHandle(process);
        return 8;
    }
    DWORD moduleBase = 0;
    GetExitCodeThread(thread, &moduleBase);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);

    if (moduleBase == 0) {
        std::printf("error: LoadLibraryW returned 0 inside pid %lu\n", static_cast<unsigned long>(pid));
        std::printf("hint: use an absolute DLL path and a 32-bit DLL\n");
        return 8;
    }
    if (verbose) {
        std::printf("injected: pid=%lu module=0x%08lX dll=%s\n",
                    static_cast<unsigned long>(pid), static_cast<unsigned long>(moduleBase),
                    dllPath.c_str());
    } else {
        std::printf("injected into pid %lu (module 0x%08lX)\n",
                    static_cast<unsigned long>(pid), static_cast<unsigned long>(moduleBase));
    }
    return 0;
}
}

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            usage();
            return 0;
        }
        if (std::strcmp(arg, "-l") == 0) {
            options.list = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') options.processName = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "-v") == 0) { options.verbose = true; continue; }
        if (std::strcmp(arg, "-p") == 0 && i + 1 < argc) { options.pid = static_cast<DWORD>(std::strtoul(argv[++i], nullptr, 10)); continue; }
        if (std::strcmp(arg, "-n") == 0 && i + 1 < argc) { options.processName = argv[++i]; continue; }
        if (std::strcmp(arg, "-i") == 0 && i + 1 < argc) { options.dllPath = argv[++i]; continue; }
        std::printf("error: unknown argument '%s'\n", arg);
        usage();
        return 2;
    }

    if (options.list) return listProcesses(options.processName);

    if (options.dllPath.empty()) {
        usage();
        return 2;
    }

    if (options.pid != 0) return injectInto(options.pid, options.dllPath, options.verbose);

    const std::wstring wanted = toWide(options.processName);
    DWORD found = 0;
    for (const auto& process : snapshotProcesses()) {
        if (_wcsicmp(process.name.c_str(), wanted.c_str()) == 0) {
            if (found != 0) {
                std::printf("error: more than one '%s' is running; pass -p <pid>\n", options.processName.c_str());
                return 3;
            }
            found = process.pid;
        }
    }
    if (found == 0) {
        std::printf("error: no process named '%s'\n", options.processName.c_str());
        return 3;
    }
    return injectInto(found, options.dllPath, options.verbose);
}
