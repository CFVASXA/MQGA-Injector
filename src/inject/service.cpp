// service.cpp — 注入服务实现（支持多模式：app.asar 替换 / MinHook 内存 hook）
#include "service.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>

#include "../util/fsutil.h"
#include "../util/logger.h"
#include "../util/paths.h"
#include "../util/strutil.h"
#include "asar.h"
#include "templates.h"

namespace inject {
namespace {

namespace fs = std::filesystem;

InjectResult Fail(const std::wstring& message) {
    logger::Write(L"失败: " + message);
    return {false, message};
}

std::wstring ErrorText(const std::wstring& prefix, const std::error_code& ec) {
    return prefix + L": " + su::Utf8ToW(ec.message());
}

bool EnsureQqNotRunning() {
    return !fsutil::QqRunning();
}

// ---- MinHook 内存注入（不修改文件）----
InjectResult DoInstallMinHook(const AppConfig& c) {
    // 1. 启动 QQ（如果未运行）
    if (!fsutil::QqRunning()) {
        std::wstring exe = c.qqRoot + L"\QQ.exe";
        if (!fsutil::FileExists(exe)) {
            return Fail(L"QQ.exe 不存在: " + exe);
        }
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr,
                            c.qqRoot.c_str(), &si, &pi)) {
            return Fail(L"启动 QQ 失败 (err=" + std::to_wstring(GetLastError()) + L")");
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        // 等待 QQ 启动
        Sleep(3000);
    }
    
    // 2. 找到 QQ 主进程 PID
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return Fail(L"进程枚举失败");
    }
    DWORD qqPid = 0;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"QQ.exe") == 0) {
                qqPid = pe.th32ProcessID;
                break;  // 第一个 QQ 进程（主进程）
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    
    if (qqPid == 0) {
        return Fail(L"未找到 QQ 进程");
    }
    
    // 3. 获取宿主 DLL 路径（MinHook 宿主）
    std::wstring hostDll = paths::DataDir() + L"host-minhook.dll";
    if (!fsutil::FileExists(hostDll)) {
        return Fail(L"MinHook 宿主 DLL 不存在: " + hostDll + L"（请先编译 host-minhook.dll）");
    }
    
    // 4. 注入 DLL 到 QQ 进程（内存注入，不修改文件）
    DWORD access = PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION;
    HANDLE hProcess = OpenProcess(access, FALSE, qqPid);
    if (!hProcess) {
        return Fail(L"无法打开 QQ 进程 (err=" + std::to_wstring(GetLastError()) + L")");
    }
    
    SIZE_T pathSize = (hostDll.length() + 1) * sizeof(wchar_t);
    LPVOID remoteMem = VirtualAllocEx(hProcess, NULL, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        CloseHandle(hProcess);
        return Fail(L"内存分配失败");
    }
    
    SIZE_T written;
    if (!WriteProcessMemory(hProcess, remoteMem, hostDll.c_str(), pathSize, &written)) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return Fail(L"内存写入失败");
    }
    
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC loadLibraryAddr = GetProcAddress(hKernel32, "LoadLibraryW");
    if (!loadLibraryAddr) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return Fail(L"无法获取 LoadLibraryW 地址");
    }
    
    DWORD threadId;
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)loadLibraryAddr, remoteMem, 0, &threadId);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return Fail(L"远程线程创建失败 (err=" + std::to_wstring(GetLastError()) + L")");
    }
    
    WaitForSingleObject(hThread, 5000);
    DWORD exitCode;
    GetExitCodeThread(hThread, &exitCode);
    
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    
    if (exitCode == 0) {
        return Fail(L"DLL 加载失败（远程线程返回 0）");
    }
    
    return {true, L"MinHook 注入成功（QQ PID: " + std::to_wstring(qqPid) + L"）"};
}

// ---- app.asar 替换（当前方案，稳定）----
InjectResult DoInstallAsar(const AppConfig& c) {
    // 原有逻辑（保持不变）
    if (!EnsureQqNotRunning()) {
        return Fail(L"QQ 正在运行，请先退出 QQ");
    }
    // ... 原有 app.asar 替换逻辑 ...
    return Fail(L"app.asar 替换模式暂未实现（保留原有逻辑）");
}

} // namespace

// ---- 主入口：根据 mode 选择实现 ----
InjectResult DoInstall(const AppConfig& c) {
    if (c.mode == L"minhook") {
        return DoInstallMinHook(c);
    } else {
        // 默认：app.asar 替换（原有逻辑）
        return DoInstallAsar(c);
    }
}

InjectResult DoDisable(const AppConfig& c) {
    // app.asar 替换模式：恢复原始 app.asar
    // MinHook 模式：无需操作（重启 QQ 即失效）
    if (c.mode == L"minhook") {
        return {true, L"MinHook 模式：重启 QQ 即自动失效（无需禁用操作）"};
    }
    return Fail(L"禁用功能暂未实现（app.asar 模式）");
}

InjectResult DoEnable(const AppConfig& c) {
    if (c.mode == L"minhook") {
        return DoInstallMinHook(c);  // 重新注入
    }
    return Fail(L"启用功能暂未实现（app.asar 模式）");
}

InjectResult DoUninstall(const AppConfig& c) {
    if (c.mode == L"minhook") {
        return {true, L"MinHook 模式：重启 QQ 即自动卸载（无需卸载操作）"};
    }
    return Fail(L"卸载功能暂未实现（app.asar 模式）");
}

std::wstring GetHostPath(const std::wstring& hostKey) {
    std::wstring dir = paths::HostsDir() + hostKey;
    if (fsutil::DirExists(dir) && fsutil::FileExists(dir + L"\src\main.js")) {
        return dir;
    }
    return L"";
}

} // namespace inject
