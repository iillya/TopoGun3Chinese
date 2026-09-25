// ============================================================================
//  TopoGun Modeler Chinese Localizer - Launcher
// ============================================================================
//  TopoGun has no plug-in entry point, so this launcher (like the Toolbag
//  localizer) starts TopoGun.exe SUSPENDED, injects the hook DLL via
//  a remote LoadLibraryW thread, then resumes. Original program files are
//  never modified. Any command line (including a scene path) is passed
//  through unchanged to TopoGun.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

#include "qt_compatibility.h"

namespace {

std::wstring ParentDir(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

bool IsFile(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ExePath() {
    std::vector<wchar_t> buf(1024);
    while (buf.size() <= 32768) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (!n) return L"";
        if (n < buf.size() - 1) return std::wstring(buf.data(), n);
        buf.resize(buf.size() * 2);
    }
    return L"";
}

std::wstring FindTopoGunRoot(const std::wstring& start) {
    std::wstring dir = start;
    for (int depth = 0; depth < 12; ++depth) {
        if (IsFile(dir + L"\\TopoGun.exe") &&
            TopoGunQtCompatibility::isAmd64Image(
                dir + L"\\TopoGun.exe")) return dir;
        std::wstring parent = ParentDir(dir);
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }
    const std::wstring installed = L"C:\\Program Files\\TopoGun 3";
    if (IsFile(installed + L"\\TopoGun.exe") &&
        TopoGunQtCompatibility::isAmd64Image(installed + L"\\TopoGun.exe")) return installed;

    return L"";
}

LPTHREAD_START_ROUTINE ResolveLoadLibrary(DWORD pid) {
    HMODULE local = GetModuleHandleW(L"kernel32.dll");
    if (!local) return nullptr;
    FARPROC fn = GetProcAddress(local, "LoadLibraryW");
    if (!fn) return nullptr;
    // Kernel32 exports can forward into KernelBase. Resolve the owning module
    // before computing its offset in the target process.
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(fn), &owner)) return nullptr;
    wchar_t ownerPath[MAX_PATH]{};
    if (!GetModuleFileNameW(owner, ownerPath, MAX_PATH)) return nullptr;
    const std::wstring ownerName = std::wstring(ownerPath).substr(
        std::wstring(ownerPath).find_last_of(L"\\/") + 1);
    uintptr_t rva = (uintptr_t)fn - (uintptr_t)owner;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W mod{}; mod.dwSize = sizeof(mod);
        if (Module32FirstW(snap, &mod)) {
            do {
                if (_wcsicmp(mod.szModule, ownerName.c_str()) == 0) {
                    CloseHandle(snap);
                    return (LPTHREAD_START_ROUTINE)((uintptr_t)mod.modBaseAddr + rva);
                }
            } while (Module32NextW(snap, &mod));
        }
        CloseHandle(snap);
    }
    // A newly created suspended process can temporarily reject module
    // snapshots on some Windows builds. Both processes have the same
    // architecture and share the boot-time system DLL mapping, so retain the
    // proven compatibility fallback used by the stable launcher.
    return reinterpret_cast<LPTHREAD_START_ROUTINE>(fn);
}

int StartOriginal(const std::wstring& root, const wchar_t* args,
                  const std::wstring& reason) {
    std::wstring command = L"\"" + root + L"\\TopoGun.exe\"";
    if (args && *args) { command += L" "; command += args; }
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool started = CreateProcessW((root + L"\\TopoGun.exe").c_str(),
        command.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &startup, &process) != FALSE;
    if (started) { CloseHandle(process.hThread); CloseHandle(process.hProcess); }
    const std::wstring message = reason + (started
        ? L"\n已启动原版 TopoGun，本次未加载汉化。"
        : L"\n原版 TopoGun 启动也失败，请直接运行原程序检查。");
    MessageBoxW(nullptr, message.c_str(), L"TopoGun 中文补丁", MB_OK | MB_ICONWARNING);
    return started ? 0 : 3;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring own = ExePath();
    const std::wstring dir = ParentDir(own);
    const std::wstring root = FindTopoGunRoot(dir);
    const std::wstring exePath = root + L"\\TopoGun.exe";

    std::wstring dllPath = dir + L"\\TopoGunChineseHook.dll";
    if (root.empty() || !IsFile(exePath)) {
        MessageBoxW(nullptr,
                    L"找不到 TopoGun.exe 或汉化 DLL。\n"
                    L"请重新运行安装程序，修复 ChineseLauncher 目录。",
                    L"TopoGun 中文补丁", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!IsFile(dllPath)) return StartOriginal(root, cmdLine, L"找不到汉化 DLL。");
    if (!TopoGunQtCompatibility::completeFamily(root, 5))
        return StartOriginal(root, cmdLine, L"汉化需要版本一致的 Qt 5.12 及以上 x64 组件。");
    const auto missing = TopoGunQtCompatibility::missingQtImport(dllPath, root);
    if (!missing.empty()) return StartOriginal(root, cmdLine, missing);

    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cl = L"\"" + exePath + L"\"";
    if (cmdLine && *cmdLine) { cl += L" "; cl += cmdLine; }
    std::vector<wchar_t> clBuf(cl.begin(), cl.end()); clBuf.push_back(L'\0');

    if (!CreateProcessW(exePath.c_str(), clBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, root.c_str(), &si, &pi)) {
        MessageBoxW(nullptr, L"无法启动 TopoGun.exe。",
                    L"TopoGun 中文补丁", MB_OK | MB_ICONERROR);
        return 2;
    }

    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(pi.hProcess, nullptr, bytes,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    SIZE_T written = 0;
    bool pathOk = remote &&
        WriteProcessMemory(pi.hProcess, remote, dllPath.c_str(), bytes, &written) &&
        written == bytes;

    auto loadLib = ResolveLoadLibrary(pi.dwProcessId);
    HANDLE thread = nullptr;
    if (pathOk && loadLib)
        thread = CreateRemoteThread(pi.hProcess, nullptr, 0, loadLib, remote, 0, nullptr);

    bool ok = false;
    bool childStopped = false;
    bool remoteThreadCompleted = false;
    if (thread) {
        DWORD wait = WaitForSingleObject(thread, 30000);
        remoteThreadCompleted = wait == WAIT_OBJECT_0;
        DWORD loadResult = 0;
        const bool loaded = wait == WAIT_OBJECT_0 &&
                            GetExitCodeThread(thread, &loadResult) &&
                            loadResult != 0;
        CloseHandle(thread);
        ok = loaded;
    }

    if (ok && ResumeThread(pi.hThread) != (DWORD)-1) {
        ok = true;
    } else {
        ok = false;
        TerminateProcess(pi.hProcess, 3);
        childStopped = WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0;
    }

    // A timed-out remote thread may still be reading the DLL path. The failure
    // path terminates the child, so let process teardown reclaim the memory.
    if (remote && remoteThreadCompleted)
        VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (!ok && !childStopped) {
        MessageBoxW(nullptr, L"汉化加载失败，且无法确认本次启动的进程已退出。为避免重复启动，请手动检查 TopoGun 进程。",
                    L"TopoGun 中文补丁", MB_OK | MB_ICONERROR);
        return 3;
    }
    return ok ? 0 : StartOriginal(root, cmdLine, L"汉化 DLL 加载失败。");
}
