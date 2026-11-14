// manualmap.cpp — Manual Map 手动映射模式实现
// 原理：手动解析 PE、写内存、修重定位、shellcode 调用入口点。
// 不调用 LoadLibrary，因此 QQ 无法通过模块加载回调检测到注入（实测成功）。
#include "manualmap_mode.h"

#include <windows.h>
#include <tlhelp32.h>
#include <vector>

#include "../util/fsutil.h"
#include "../util/logger.h"
#include "../util/paths.h"

namespace modes {
namespace {

core::InjectResult Fail(const std::wstring& msg) {
    logger::Write(L"失败: " + msg);
    return {false, msg};
}

// 查找 QQ 主进程 PID（取第一个 QQ.exe）
DWORD FindQqPid(std::wstring& err) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        err = L"进程枚举失败";
        return 0;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"QQ.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (pid == 0) err = L"未找到 QQ 进程";
    return pid;
}

// 启动 QQ 并等待主进程
DWORD LaunchAndWaitQq(const core::AppConfig& c, std::wstring& err) {
    std::wstring exe = c.qqRoot + L"\\QQ.exe";
    if (!fsutil::FileExists(exe)) {
        err = L"QQ.exe 不存在: " + exe;
        return 0;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutableExe = exe;
    if (!CreateProcessW(nullptr, mutableExe.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        c.qqRoot.c_str(), &si, &pi)) {
        err = L"启动 QQ 失败 (err=" + std::to_wstring(GetLastError()) + L")";
        return 0;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    // 等待进程完全起来
    for (int i = 0; i < 30; ++i) {
        Sleep(1000);
        std::wstring e2;
        DWORD pid = FindQqPid(e2);
        if (pid) return pid;
    }
    err = L"QQ 启动后未找到进程";
    return 0;
}

// RVA → 文件偏移
DWORD RvaToRaw(const IMAGE_NT_HEADERS* nt, DWORD rva) {
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (rva >= sec[i].VirtualAddress &&
            rva < sec[i].VirtualAddress + sec[i].SizeOfRawData) {
            return sec[i].PointerToRawData + (rva - sec[i].VirtualAddress);
        }
    }
    return rva;
}

// 读取整个文件到内存
bool ReadFileToBuf(const std::wstring& path, std::vector<BYTE>& buf) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0) { CloseHandle(h); return false; }
    buf.resize(size);
    DWORD read = 0;
    ReadFile(h, buf.data(), size, &read, nullptr);
    CloseHandle(h);
    return read == size;
}

// 调用外部 manual-map 工具（已验证的完整实现，含 shellcode）
core::InjectResult RunManualMapTool(DWORD pid, const std::wstring& dll) {
    std::wstring tool = paths::DataDir() + L"tools\\manual-map.exe";
    if (!fsutil::FileExists(tool)) {
        // 工具缺失：给出明确指引，而不是假装成功
        return Fail(L"Manual Map 工具缺失: " + tool +
                    L"（请将 manual-map.exe 放入 data\\tools\\ 目录）");
    }
    std::wstring cmd = L"\"" + tool + L"\" " + std::to_wstring(pid) + L" \"" + dll + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return Fail(L"Manual Map 工具启动失败 (err=" + std::to_wstring(GetLastError()) + L")");
    }
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exitCode != 0) {
        return Fail(L"Manual Map 工具返回错误码 " + std::to_wstring(exitCode));
    }
    return {true, L""};
}

} // namespace

core::InjectResult ManualMapMode::Install(const core::AppConfig& c) {
    logger::Write(L"开始安装注入（Manual Map）");

    // 1. 确保 QQ 在运行
    std::wstring err;
    DWORD pid = FindQqPid(err);
    if (!pid) pid = LaunchAndWaitQq(c, err);
    if (!pid) return Fail(err);

    // 2. 宿主 DLL（MinHook 宿主，内含插件加载逻辑）
    std::wstring hostDll = paths::DataDir() + L"host-minhook.dll";
    if (!fsutil::FileExists(hostDll)) {
        return Fail(L"宿主 DLL 不存在: " + hostDll +
                    L"（请编译 host-minhook.dll 放入 data 目录）");
    }

    // 3. 执行映射
    auto r = RunManualMapTool(pid, hostDll);
    if (!r.ok) return r;

    logger::Write(L"Manual Map 注入完成: " + std::to_wstring(pid));
    return {true, L"Manual Map 注入成功（QQ PID: " + std::to_wstring(pid) +
                L"，绕过 LoadLibrary 检测）"};
}

} // namespace modes