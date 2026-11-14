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

std::wstring ResourcesPath(const AppConfig& c) {
    return (fs::path(c.qqRoot) / L"versions" / c.version / L"resources").wstring();
}

std::wstring ShadowPath(const AppConfig& c) {
    return (fs::path(ResourcesPath(c)) / L"app.asar").wstring();
}

bool SamePath(const std::wstring& a, const std::wstring& b) {
    std::error_code ec;
    auto left = fs::weakly_canonical(fs::path(a), ec).wstring();
    if (ec) left = fs::path(a).lexically_normal().wstring();
    auto right = fs::weakly_canonical(fs::path(b), ec).wstring();
    if (ec) right = fs::path(b).lexically_normal().wstring();
    while (left.size() > 3 && left.back() == L'\\') left.pop_back();
    while (right.size() > 3 && right.back() == L'\\') right.pop_back();
    std::transform(left.begin(), left.end(), left.begin(), ::towlower);
    std::transform(right.begin(), right.end(), right.begin(), ::towlower);
    return left == right;
}

bool DeleteOneFile(const std::wstring& path, std::wstring& message) {
    if (!DeleteFileW(path.c_str())) {
        message = L"删除文件失败: " + path + L" (err=" + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    return true;
}

bool MoveOneFile(const std::wstring& from, const std::wstring& to, std::wstring& message) {
    if (!MoveFileW(from.c_str(), to.c_str())) {
        message = L"移动文件失败: " + from + L" -> " + to +
            L" (err=" + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    return true;
}

} // namespace

InjectResult DoInstallMinHook(const AppConfig& c);

InjectResult DoInstallProxyDll(const AppConfig& c);
InjectResult DoInstallEnvInject(const AppConfig& c);
InjectResult DoInstallIfeo(const AppConfig& c);

InjectResult DoInstall(const AppConfig& c) {
    // MinHook 模式：内存注入（不修改文件）
    if (c.mode == L"minhook") {
        return DoInstallMinHook(c);
    }
    // 代理 ffmpeg.dll：不触发文件校验（第三方库）
    if (c.mode == L"proxydll") {
        return DoInstallProxyDll(c);
    }
    // 环境变量注入：NODE_OPTIONS 注入 preload（需 QQ 未禁用）
    if (c.mode == L"envinject") {
        return DoInstallEnvInject(c);
    }
    // IFEO 调试器注入：注册表合法机制
    if (c.mode == L"ifeo") {
        return DoInstallIfeo(c);
    }
    // app.asar 替换模式（原有逻辑）
    logger::Write(L"开始安装注入");
    if (!EnsureQqNotRunning()) return Fail(L"QQ 正在运行，请先退出 QQ");

    const std::wstring resources = ResourcesPath(c);
    const std::wstring app = resources + L"\\app";
    const std::wstring applicationJson = app + L"\\application.json";
    const std::wstring applicationAsar = app + L"\\application.asar";
    if (!fsutil::FileExists(applicationJson))
        return Fail(L"QQ 目录缺少 application.json: " + applicationJson);
    if (!fsutil::FileExists(applicationAsar))
        return Fail(L"QQ 目录缺少 application.asar: " + applicationAsar);

    const std::wstring hostRoot = c.hostSourceDir;
    const std::wstring hostMain = hostRoot + L"\\src\\main.js";
    if (!fsutil::FileExists(hostMain))
        return Fail(L"宿主源码缺少 src\\main.js: " + hostRoot);

    std::error_code ec;
    fs::create_directories(c.installDir, ec);
    if (ec) return Fail(ErrorText(L"创建安装目录失败: " + c.installDir, ec));
    fs::create_directories(fs::path(c.installDir) / L"logs", ec);
    if (ec) return Fail(ErrorText(L"创建日志目录失败: " + c.installDir, ec));

    std::wstring targetHost;
    if (fsutil::IsPathInside(hostRoot, c.installDir)) {
        targetHost = hostRoot;
    } else {
        std::wstring name = fs::path(hostRoot).filename().wstring();
        if (name.empty()) name = c.host;
        if (name.empty()) return Fail(L"无法确定宿主目录名: " + hostRoot);
        targetHost = (fs::path(c.installDir) / name).wstring();
    }

    if (!SamePath(hostRoot, targetHost)) {
        logger::Write(L"复制宿主: " + hostRoot + L" -> " + targetHost);
        std::wstring err;
        if (!fsutil::CopyDir(hostRoot, targetHost, err)) {
            std::wstring cleanupErr;
            fsutil::RemoveDirIfExists(targetHost, cleanupErr);
            if (!cleanupErr.empty()) logger::Write(cleanupErr);
            return Fail(err.empty() ? L"复制宿主失败" : err);
        }
    }

    const std::wstring shadow = resources + L"\\app.asar";
    std::wstring err;
    const std::string archive = AsarPack(BuildShadowFiles(targetHost, c.version, resources), err);
    if (archive.empty()) return Fail(err.empty() ? L"ASAR 生成结果为空" : err);
    if (!fsutil::WriteFileAtomic(shadow, archive, err)) return Fail(err);

    logger::Write(L"shadow 已写入: " + shadow);
    return {true, L"安装完成: " + shadow};
}

InjectResult DoDisable(const AppConfig& c) {
    logger::Write(L"开始禁用注入");
    if (!EnsureQqNotRunning()) return Fail(L"QQ 正在运行，请先退出 QQ");
    const std::wstring path = ShadowPath(c);
    const std::wstring disabled = path + L".disabled";
    if (!fsutil::FileExists(path)) return Fail(L"app.asar 不存在: " + path);
    if (fsutil::FileExists(disabled)) {
        std::wstring err;
        if (!DeleteOneFile(disabled, err)) return Fail(err);
    }
    std::wstring err;
    if (!MoveOneFile(path, disabled, err)) return Fail(err);
    logger::Write(L"已禁用: " + path + L" -> " + disabled);
    return {true, L"已禁用注入"};
}

InjectResult DoEnable(const AppConfig& c) {
    logger::Write(L"开始恢复注入");
    if (!EnsureQqNotRunning()) return Fail(L"QQ 正在运行，请先退出 QQ");
    const std::wstring path = ShadowPath(c);
    const std::wstring disabled = path + L".disabled";
    if (!fsutil::FileExists(disabled)) return Fail(L"没有可恢复的 .disabled 文件: " + disabled);
    if (fsutil::FileExists(path)) return Fail(L"app.asar 已存在: " + path);
    std::wstring err;
    if (!MoveOneFile(disabled, path, err)) return Fail(err);
    logger::Write(L"已恢复: " + disabled + L" -> " + path);
    return {true, L"已恢复注入"};
}

InjectResult DoUninstall(const AppConfig& c) {
    // 分模式卸载
    if (c.mode == L"minhook") {
        return {true, L"MinHook 为内存注入，重启 QQ 即完全失效，无需文件清理"};
    }
    if (c.mode == L"proxydll") {
        if (!EnsureQqNotRunning()) return Fail(L"QQ 正在运行，请先退出 QQ");
        const std::wstring verDir = c.qqRoot + L"\\\\versions\\\\" + c.version;
        const std::wstring target = verDir + L"\ffmpeg.dll";
        const std::wstring backup = verDir + L"\ffmpeg_orig.dll";
        if (!fsutil::FileExists(backup)) return {true, L"未找到备份（未安装代理或已卸载）"};
        std::error_code ec;
        fs::copy_file(backup, target, fs::copy_options::overwrite_existing, ec);
        if (ec) return Fail(L"恢复原 ffmpeg.dll 失败");
        fs::remove(backup, ec);
        return {true, L"已恢复原 ffmpeg.dll"};
    }
    if (c.mode == L"envinject") {
        return {true, L"环境变量注入仅作用于单次启动，无需卸载"};
    }
    if (c.mode == L"ifeo") {
        const wchar_t* subKey = L"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\QQ.exe";
        LONG rc = RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, subKey, L"Debugger");
        if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
            return Fail(L"清除 IFEO 失败（需要管理员权限）");
        }
        return {true, L"IFEO 已清除"};
    }
    logger::Write(L"开始卸载注入");
    if (!EnsureQqNotRunning()) return Fail(L"QQ 正在运行，请先退出 QQ");
    const std::wstring path = ShadowPath(c);
    const std::wstring suffixes[] = {L"", L".disabled", L".bak", L".tmp"};
    std::wstring removed;
    for (const auto& suffix : suffixes) {
        const std::wstring file = path + suffix;
        if (!fsutil::FileExists(file)) continue;
        std::wstring err;
        if (!DeleteOneFile(file, err)) return Fail(err);
        if (!removed.empty()) removed += L", ";
        removed += fs::path(file).filename().wstring();
    }
    logger::Write(L"已卸载: " + (removed.empty() ? std::wstring(L"无 shadow 文件") : removed));
    return {true, removed.empty() ? L"shadow 文件不存在" : L"已删除: " + removed};
}

std::wstring GetHostPath(const std::wstring& hostKey) {
    const std::wstring path = (fs::path(paths::HostsDir()) / hostKey).wstring();
    return fsutil::DirExists(path) ? path : L"";
}


// ---- MinHook 内存注入（不修改文件）----
InjectResult DoInstallMinHook(const AppConfig& c) {
    // 1. 启动 QQ（如果未运行）
    if (!fsutil::QqRunning()) {
        std::wstring exe = c.qqRoot + L"\\QQ.exe";
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
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    
    if (qqPid == 0) {
        return Fail(L"未找到 QQ 进程");
    }
    
    // 3. 获取宿主 DLL 路径
    std::wstring hostDll = paths::DataDir() + L"host-minhook.dll";
    if (!fsutil::FileExists(hostDll)) {
        return Fail(L"MinHook 宿主 DLL 不存在: " + hostDll);
    }
    
    // 4. 注入 DLL 到 QQ 进程
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
        return Fail(L"DLL 加载失败");
    }
    
    return {true, L"MinHook 注入成功（QQ PID: " + std::to_wstring(qqPid) + L"）"};
}


// ---- 代理 ffmpeg.dll（不触发文件校验，QQ 更新会覆盖需重装）----
InjectResult DoInstallProxyDll(const AppConfig& c) {
    if (!EnsureQqNotRunning()) return Fail(L"QQ 正在运行，请先退出 QQ");
    const std::wstring verDir = c.qqRoot + L"\\\\versions\\\\" + c.version;
    const std::wstring target = verDir + L"\ffmpeg.dll";
    const std::wstring backup = verDir + L"\ffmpeg_orig.dll";
    const std::wstring proxy = paths::DataDir() + L"ffmpeg-proxy.dll";
    if (!fsutil::FileExists(proxy)) {
        return Fail(L"代理 DLL 不存在: " + proxy + L"（需先编译 ffmpeg-proxy.dll 放入 data 目录）");
    }
    if (!fsutil::FileExists(target)) {
        return Fail(L"目标 ffmpeg.dll 不存在: " + target + L"（请确认 QQ 版本目录正确）");
    }
    std::error_code ec;
    // 已安装过则跳过备份（避免把代理当原 DLL 备份）
    if (!fsutil::FileExists(backup)) {
        fs::copy_file(target, backup, fs::copy_options::overwrite_existing, ec);
        if (ec) return Fail(L"备份原 ffmpeg.dll 失败: " + su::Utf8ToW(ec.message()));
    }
    fs::copy_file(proxy, target, fs::copy_options::overwrite_existing, ec);
    if (ec) return Fail(L"写入代理 DLL 失败: " + su::Utf8ToW(ec.message()));
    logger::Write(L"代理 ffmpeg.dll 已部署");
    return {true, L"代理 ffmpeg.dll 注入成功（不触发文件校验）"};
}

// ---- 环境变量注入（NODE_OPTIONS --require preload）----
InjectResult DoInstallEnvInject(const AppConfig& c) {
    // 把 preload.js 放到 data 目录，然后用带 NODE_OPTIONS 的环境启动 QQ
    const std::wstring preload = paths::DataDir() + L"preload.js";
    if (!fsutil::FileExists(preload)) {
        return Fail(L"preload.js 不存在: " + preload + L"（需先在设置页生成）");
    }
    if (fsutil::QqRunning()) return Fail(L"QQ 正在运行，请先退出 QQ");
    std::wstring exe = c.qqRoot + L"\QQ.exe";
    if (!fsutil::FileExists(exe)) return Fail(L"QQ.exe 不存在: " + exe);

    // 用 cmd 设置进程级环境变量并启动（只影响该进程，不污染系统环境）
    std::wstring cmd = L"cmd.exe /c set NODE_OPTIONS=--require \"" + preload + L"\" && \"" + exe + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE, nullptr, c.qqRoot.c_str(), &si, &pi)) {
        return Fail(L"带 NODE_OPTIONS 启动 QQ 失败 (err=" + std::to_wstring(GetLastError()) + L")");
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return {true, L"已用 NODE_OPTIONS 启动 QQ（若 QQ 禁用该机制则无效果，请看日志）"};
}

// ---- IFEO 调试器注入（注册表合法机制，需管理员权限）----
InjectResult DoInstallIfeo(const AppConfig& c) {
    const std::wstring debugger = paths::DataDir() + L"ifeo-launcher.exe";
    if (!fsutil::FileExists(debugger)) {
        return Fail(L"IFEO 启动器不存在: " + debugger + L"（需先编译 ifeo-launcher.exe）");
    }
    // 写注册表：HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\QQ.exe
    // Debugger = <launcher path>
    HKEY hKey;
    const wchar_t* subKey = L"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\QQ.exe";
    LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) {
        return Fail(L"无法打开 IFEO 注册表项（需要管理员权限运行注入器）");
    }
    rc = RegSetValueExW(hKey, L"Debugger", 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(debugger.c_str()),
                        static_cast<DWORD>((debugger.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) {
        return Fail(L"写入 Debugger 值失败");
    }
    logger::Write(L"IFEO 已注册: " + debugger);
    return {true, L"IFEO 已注册（下次启动 QQ 时经启动器加载，卸载请在「卸载」里清除）"};
}

} // namespace inject
