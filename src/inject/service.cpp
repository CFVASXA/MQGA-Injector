#include "service.h"

#include <windows.h>

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

InjectResult DoInstall(const AppConfig& c) {
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

} // namespace inject
