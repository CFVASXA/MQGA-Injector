// bridge.cpp — 消息分发实现。后台线程执行操作，进度/结果回发 UI。
#include "bridge.h"
#include "../util/strutil.h"
#include "../util/config.h"
#include "../util/logger.h"
#include "../util/paths.h"
#include "../util/fsutil.h"
#include "../qq/locate.h"
#include "../net/download.h"
#include "../net/unzip.h"
#include "../inject/service.h"
#include "pickfolder.h"
#include <windows.h>
#include <tlhelp32.h>
#include <thread>
#include <atomic>
#include <functional>

namespace ui {

static PostFn g_post = nullptr;
static std::atomic<bool> g_busy{ false };

void SetPostFn(PostFn fn) { g_post = fn; }

static void Post(const std::wstring& utf8Json)
{
    if (g_post) g_post(utf8Json);
}

static void PostEvent(const std::string& event, const std::string& jsonFields)
{
    // {"event":"...","fields":...}
    std::string s = "{\"event\":";
    s += su::JsonStr(su::Utf8ToW(event));
    if (!jsonFields.empty()) {
        s += ",";
        s += jsonFields;
    }
    s += "}";
    Post(su::Utf8ToW(s));
}

static void PostBusy(bool b)
{
    PostEvent("busy", std::string("\"busy\":") + (b ? "true" : "false"));
}

static void PostProgress(const std::wstring& msg)
{
    PostEvent("progress", "\"message\":" + su::JsonStr(msg));
    logger::Write(msg);
}

static void PostResult(bool ok, const std::wstring& msg)
{
    // 操作结果落文件日志（用户反馈"日志不会写"：此前只有 progress 入日志）
    logger::Write(std::wstring(ok ? L"[OK] " : L"[FAIL] ") + msg);
    PostEvent("result", "\"ok\":" + std::string(ok ? "true" : "false") +
             ",\"message\":" + su::JsonStr(msg));
}

// ---- 下载：下载 + 解压，完成后 Post downloaded 事件 ----
static void DoDownloadWork(const std::wstring& hostKey)
{
    std::wstring dlErr;
    int rc = net::DownloadHost(hostKey, [](int percent, const std::wstring& msg) {
        PostProgress(msg + L" (" + std::to_wstring(percent) + L"%)");
        }, dlErr);
    if (rc != 0) {
        PostResult(false, L"下载失败: " + dlErr);
        return;
    }
    std::wstring extractErr;
    std::wstring dir = net::ExtractHost(hostKey, extractErr);
    if (dir.empty()) {
        PostResult(false, L"解压失败: " + extractErr);
        return;
    }
    // {"event":"downloaded","path":"..."}
    PostEvent("downloaded", "\"path\":" + su::JsonStr(dir) + ",\"ok\":true");
}

// ---- 配置合并：以已存配置为底，仅覆盖消息里非空的字段 ----
// 修复：此前直接用消息字段构造完整配置，JS 端漏带字段会把已存配置清空。
static AppConfig MergeConfig(const std::map<std::wstring, std::wstring>& m)
{
    AppConfig c = config::Load(); // 底：磁盘已存配置
    auto overlay = [&](const wchar_t* key, std::wstring& dst) {
        auto it = m.find(key);
        if (it != m.end() && !it->second.empty()) dst = it->second;
    };
    overlay(L"host", c.host);
    overlay(L"hostSourceDir", c.hostSourceDir);
    overlay(L"installDir", c.installDir);
    overlay(L"qqRoot", c.qqRoot);
    overlay(L"version", c.version);
    return c;
}

static void ActInstall(const std::map<std::wstring, std::wstring>& m)
{
    AppConfig c = MergeConfig(m);
    auto r = inject::DoInstall(c);
    // 只有成功才落盘配置——失败不该污染上次可用的配置
    if (r.ok) config::Save(c);
    PostResult(r.ok, r.message);
}

// disable/enable/uninstall：只操作文件系统，不动配置
static void ActSimple(const std::map<std::wstring, std::wstring>& m,
                      inject::InjectResult(*fn)(const AppConfig&))
{
    AppConfig c = MergeConfig(m);
    auto r = fn(c);
    PostResult(r.ok, r.message);
}

static void ActLocateQq()
{
    auto path = qq::FindQqRoot();
    PostEvent("qqRoot", "\"path\":" + su::JsonStr(path));
}

static void ActListVersions(const std::map<std::wstring, std::wstring>& m)
{
    auto root = m.count(L"qqRoot") ? m.at(L"qqRoot") : L"";
    auto versions = qq::ListVersions(root);
    std::string arr = "\"versions\":[";
    for (size_t i = 0; i < versions.size(); ++i) {
        if (i) arr += ",";
        arr += su::JsonStr(versions[i]);
    }
    arr += "]";
    PostEvent("versions", arr);
}

// 路径校验：ok=false 是阻断性错误；installDir 不存在只是提示（自动创建），用 warn 区分
static void ActValidatePath(const std::map<std::wstring, std::wstring>& m)
{
    auto path = m.count(L"path") ? m.at(L"path") : L"";
    auto kind = m.count(L"kind") ? m.at(L"kind") : L"";
    if (path.empty()) {
        PostEvent("validate", "\"kind\":" + su::JsonStr(kind) + ",\"ok\":false,\"warn\":false,\"reason\":" + su::JsonStr(L"未填写"));
        return;
    }
    std::wstring reason;
    bool ok = false, warn = false;
    if (kind == L"qqRoot") {
        ok = fsutil::FileExists(path + L"\\QQ.exe");
        reason = ok ? L"QQ.exe 已找到" : L"目录下没有 QQ.exe";
    } else if (kind == L"hostSourceDir") {
        ok = fsutil::FileExists(path + L"\\src\\main.js");
        reason = ok ? L"宿主源码有效" : L"缺少 src\\main.js";
    } else if (kind == L"installDir") {
        if (fsutil::DirExists(path)) { ok = true; reason = L"目录存在"; }
        else { ok = true; warn = true; reason = L"目录不存在，安装时会自动创建"; }
    }
    PostEvent("validate", "\"kind\":" + su::JsonStr(kind) +
              ",\"ok\":" + std::string(ok ? "true" : "false") +
              ",\"warn\":" + std::string(warn ? "true" : "false") +
              ",\"reason\":" + su::JsonStr(reason));
}

static void ActCheckUpdate(const std::map<std::wstring, std::wstring>& m)
{
    auto qqRoot = m.count(L"qqRoot") ? m.at(L"qqRoot") : L"";
    auto versions = qq::ListVersions(qqRoot); // 倒序（新→旧）
    std::wstring injected;
    for (auto& v : versions) {
        if (fsutil::FileExists(qqRoot + L"\\versions\\" + v + L"\\resources\\app.asar")) {
            injected = v;
            break;
        }
    }
    std::wstring pending;
    for (auto& v : versions) {
        if (v == injected) break;
        pending = v;
        break;
    }
    std::string state = injected.empty() ? "none" : (!pending.empty() ? "update" : "ok");
    std::string s = "\"state\":\"" + state + "\"" +
        ",\"injectedVersion\":" + su::JsonStr(injected) +
        ",\"pendingVersion\":" + su::JsonStr(pending);
    PostEvent("qqUpdate", s);
}

// 宿主列表：扫描 data/hosts/ 下已下载的宿主
static void ActListHosts(const std::map<std::wstring, std::wstring>&)
{
    std::wstring hostsDir = paths::HostsDir();
    if (!fsutil::DirExists(hostsDir)) {
        PostEvent("hosts", "\"items\":[]");
        PostResult(true, L"宿主目录为空（先到宿主管理页下载）");
        return;
    }
    std::string items;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((hostsDir + L"/*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;
                // 有 src/main.js 才是完整宿主
                std::wstring root = hostsDir + L"/" + name;
                bool hasMain = fsutil::FileExists(root + L"/src/main.js");
                if (!items.empty()) items += ",";
                items += "{\"name\":" + su::JsonStr(name) + ",\"ready\":" +
                         (hasMain ? "true" : "false") + "}";
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    PostEvent("hosts", "\"items\":[" + items + "]");
    PostResult(true, L"宿主列表已刷新");
}

// 插件列表：扫描 installDir/plugins/*/manifest.json
static void ActListPlugins(const std::map<std::wstring, std::wstring>& m)
{
    auto installDir = m.count(L"installDir") ? m.at(L"installDir") : L"";
    if (installDir.empty()) { PostResult(false, L"安装目录为空"); return; }
    std::wstring dir = installDir + L"/plugins";
    if (!fsutil::DirExists(dir)) { PostResult(false, L"未找到 plugins 目录（宿主未安装?）"); return; }

    std::string items;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"/*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;
                std::wstring mf = dir + L"/" + name + L"/manifest.json";
                std::wstring ver, author, desc;
                HANDLE fh = CreateFileW(mf.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (fh != INVALID_HANDLE_VALUE) {
                    char buf[8192]; DWORD got = 0;
                    std::string content;
                    while (ReadFile(fh, buf, sizeof(buf), &got, nullptr) && got) {
                        content.append(buf, got);
                        if (content.size() > 65536) break;
                    }
                    CloseHandle(fh);
                    auto kv = su::JsonParseFlat(content);
                    if (kv.count(L"version")) ver = kv[L"version"];
                    if (kv.count(L"author"))  author = kv[L"author"];
                    if (kv.count(L"description")) desc = kv[L"description"];
                }
                // JsonStr 负责转义（含引号/反斜杠/控制符）
                std::string uName = su::JsonStr(name);
                std::string uVer  = su::JsonStr(ver);
                std::string uAuth = su::JsonStr(author);
                std::string uDesc = su::JsonStr(desc);
                if (!items.empty()) items += ",";
                items += "{\"name\":" + uName + ",\"version\":" + uVer
                       + ",\"author\":" + uAuth + ",\"description\":" + uDesc + "}";
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    PostEvent("plugins", "\"items\":[" + items + "]");
    PostResult(true, L"插件列表已刷新");
}

// 工具：生成 preload.js（环境变量注入用）
static void ActGenPreload(const std::map<std::wstring, std::wstring>&)
{
    const std::wstring target = paths::DataDir() + L"preload.js";
    const char* content = R"PRELOAD(// preload.js - NODE_OPTIONS 注入入口（由 MQGA 注入器生成）
const fs = require('fs');
const path = require('path');
const logFile = path.join(process.env.TEMP || '/tmp', 'mqga-preload.log');
try {
    fs.appendFileSync(logFile, '[preload] loaded at ' + new Date().toISOString() + '\n');
    // TODO: 在这里加载宿主（MakeQQGreatAgain / LiteLoaderQQNT）
} catch (e) {
    try { fs.appendFileSync(logFile, '[preload] error: ' + e.message + '\n'); } catch (_) {}
}
)PRELOAD";
    std::wstring err;
    if (!fsutil::WriteFileAtomic(target, content, err)) {
        PostResult(false, L"写入失败: " + err);
        return;
    }
    PostResult(true, L"preload.js 已生成: " + target);
}

// 工具：检测 MinHook 宿主 DLL 是否就绪
static void ActCheckMinhook(const std::map<std::wstring, std::wstring>&)
{
    const std::wstring dll = paths::DataDir() + L"host-minhook.dll";
    bool ok = fsutil::FileExists(dll);
    PostEvent("toolStatus", "\"kind\":\"minhook\",\"ready\":" + std::string(ok ? "true" : "false") +
              ",\"path\":" + su::JsonStr(dll));
    PostResult(ok, ok ? L"MinHook 宿主 DLL 就绪" : L"缺少 host-minhook.dll（请先编译放入 data 目录）");
}

// 工具：检测代理 ffmpeg.dll 是否就绪
static void ActCheckProxy(const std::map<std::wstring, std::wstring>&)
{
    const std::wstring dll = paths::DataDir() + L"ffmpeg-proxy.dll";
    bool ok = fsutil::FileExists(dll);
    PostEvent("toolStatus", "\"kind\":\"proxy\",\"ready\":" + std::string(ok ? "true" : "false") +
              ",\"path\":" + su::JsonStr(dll));
    PostResult(ok, ok ? L"代理 DLL 就绪" : L"缺少 ffmpeg-proxy.dll（请先编译放入 data 目录）");
}

// 启动 QQ 验证注入效果（防双开：已在运行则提示而非再起一份）
static void ActLaunchQq(const std::map<std::wstring, std::wstring>& m)
{
    if (fsutil::QqRunning()) {
        PostResult(false, L"QQ 已在运行（无需重复启动）");
        return;
    }
    auto qqRoot = m.count(L"qqRoot") ? m.at(L"qqRoot") : L"";
    if (qqRoot.empty()) { PostResult(false, L"QQ 目录为空"); return; }
    std::wstring exe = qqRoot + L"\\QQ.exe";
    if (!fsutil::FileExists(exe)) { PostResult(false, L"QQ.exe 不存在: " + exe); return; }
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr,
                        qqRoot.c_str(), &si, &pi)) {
        PostResult(false, L"启动失败 (err=" + std::to_wstring(GetLastError()) + L")");
        return;
    }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    PostResult(true, L"QQ 已启动");
}

// 一键退出全部 QQ
static void ActKillQq(const std::map<std::wstring, std::wstring>&)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) { PostResult(false, L"进程枚举失败"); return; }
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    int killed = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"QQ.exe") == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { if (TerminateProcess(h, 0)) ++killed; CloseHandle(h); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (killed == 0) { PostResult(false, L"QQ 未在运行"); return; } // 杀 0 个不算成功
    PostResult(true, L"已退出 " + std::to_wstring(killed) + L" 个 QQ 进程");
}

static void ActOpenFolder(const std::map<std::wstring, std::wstring>& m)
{
    std::wstring p = m.count(L"path") ? m.at(L"path") : L"";
    if (p == L"__hosts__") p = paths::HostsDir();
    else if (p == L"__data__") p = paths::DataDir();
    if (!fsutil::DirExists(p)) { PostResult(false, L"目录不存在: " + p); return; }
    // explorer 打开（ShellExecuteW 在 shell32；用 CreateProcessW 调 explorer.exe 规避缺失 shell32.lib）
    std::wstring cmd = L"explorer.exe \"" + p + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        PostResult(false, L"打开失败");
        return;
    }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    PostResult(true, L"已打开: " + p);
}

void HandleMessage(const std::string& utf8Json)
{
    auto m = su::JsonParseFlat(utf8Json);
    auto it = m.find(L"action");
    if (it == m.end()) return;
    std::wstring action = it->second;
    logger::Write(L"收到操作: " + action);

    // jsAck：JS 确认收到回发消息（闭环验证），轻量处理不占 busy 锁
    if (action == L"jsAck") {
        auto ev = m.find(L"event");
        logger::Write(L"  JS 确认收到事件: " + (ev != m.end() ? ev->second : L"?"));
        return;
    }

    // pickFolder：必须在 UI 线程弹对话框（COM STA），不进后台线程
    if (action == L"pickFolder") {
        auto kind = m.count(L"kind") ? m.at(L"kind") : L"";
        auto initial = m.count(L"initial") ? m.at(L"initial") : L"";
        std::wstring title = (kind == L"qqRoot") ? L"选择 QQ 安装目录"
                           : (kind == L"installDir") ? L"选择宿主安装目录"
                           : L"选择宿主源码目录";
        auto picked = PickFolder(title, initial);
        PostEvent("picked", "\"kind\":" + su::JsonStr(kind) +
                  ",\"path\":" + su::JsonStr(picked));
        return;
    }

    // ready：回发配置（此时 JS 监听器已注册）
    if (action == L"ready") {
        auto c = config::Load();
        // 首次运行：qqRoot 为空时自动检测，免手动填
        if (c.qqRoot.empty()) {
            auto found = qq::FindQqRoot();
            if (!found.empty()) {
                c.qqRoot = found;
                logger::Write(L"[init] 首次运行，自动检测 QQ: " + found);
            }
        }
        std::string s = "{\"event\":\"init\",\"config\":{";
        s += "\"host\":" + su::JsonStr(c.host) + ",";
        s += "\"hostSourceDir\":" + su::JsonStr(c.hostSourceDir) + ",";
        s += "\"installDir\":" + su::JsonStr(c.installDir) + ",";
        s += "\"qqRoot\":" + su::JsonStr(c.qqRoot) + ",";
        s += "\"version\":" + su::JsonStr(c.version);
        s += "}}";
        Post(su::Utf8ToW(s));
        return;
    }

    if (g_busy.exchange(true)) return; // 上一个操作还在执行，丢弃
    PostBusy(true);

    // 重活放后台线程，避免卡 UI 消息循环
    std::thread([action, m]() {
        try {
            if (action == L"install")            ActInstall(m);
            else if (action == L"disable")       ActSimple(m, &inject::DoDisable);
            else if (action == L"enable")        ActSimple(m, &inject::DoEnable);
            else if (action == L"uninstall")     ActSimple(m, &inject::DoUninstall);
            else if (action == L"download")      DoDownloadWork(m.count(L"host") ? m.at(L"host") : L"mqga");
            else if (action == L"locateQq")      ActLocateQq();
            else if (action == L"listVersions")  ActListVersions(m);
            else if (action == L"getHostPath") {
                auto dir = inject::GetHostPath(m.count(L"host") ? m.at(L"host") : L"mqga");
                if (dir.empty()) PostResult(false, L"宿主尚未下载");
                else PostEvent("downloaded", "\"path\":" + su::JsonStr(dir) + ",\"ok\":true");
            }
            else if (action == L"openFolder")    ActOpenFolder(m);
            else if (action == L"validatePath")  ActValidatePath(m);
            else if (action == L"checkUpdate")   ActCheckUpdate(m);
            else if (action == L"launchQq")      ActLaunchQq(m);
            else if (action == L"killQq")        ActKillQq(m);
            else if (action == L"listPlugins")   ActListPlugins(m);
            else if (action == L"listHosts")     ActListHosts(m);
            else if (action == L"genPreload")    ActGenPreload(m);
            else if (action == L"checkMinhook")  ActCheckMinhook(m);
            else if (action == L"checkProxy")    ActCheckProxy(m);
            else PostResult(false, L"未知操作: " + action);
        } catch (const std::exception& ex) {
            PostResult(false, su_cvt(ex.what()));
        } catch (...) {
            PostResult(false, L"未知错误");
        }
        g_busy = false; // 释放操作锁（此前从未复位，第二个操作起全部被丢弃）
        PostBusy(false);
    }).detach();
}

} // namespace ui
