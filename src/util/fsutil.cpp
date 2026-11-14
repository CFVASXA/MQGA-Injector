// fsutil.cpp — 文件系统辅助实现。
#include "fsutil.h"
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <algorithm>

std::wstring su_cvt(const char* s);

namespace fs = std::filesystem;

namespace fsutil {

static std::wstring NormTrail(std::wstring s)
{
    if (!s.empty() && s.back() != L'\\') s += L'\\';
    return s;
}

bool FileExists(const std::wstring& p) { return fs::exists(p) && fs::is_regular_file(p); }
bool DirExists(const std::wstring& p)  { return fs::exists(p) && fs::is_directory(p); }

bool CopyDir(const std::wstring& src, const std::wstring& dst, std::wstring& err)
{
    try {
        std::error_code ec;
        fs::create_directories(dst, ec);
        for (auto& e : fs::recursive_directory_iterator(src)) {
            auto rel = fs::relative(e.path(), src);
            auto target = fs::path(dst) / rel;
            if (e.is_directory()) {
                fs::create_directories(target, ec);
            } else {
                fs::create_directories(target.parent_path(), ec);
                fs::copy_file(e.path(), target, fs::copy_options::overwrite_existing, ec);
                if (ec) { err = L"复制失败: " + e.path().wstring() + L" -> " + su_cvt(ec.message().c_str()); return false; }
            }
        }
        return true;
    } catch (const std::exception& ex) {
        err = su_cvt(ex.what());
        return false;
    }
}

bool RemoveDirIfExists(const std::wstring& dir, std::wstring& err)
{
    std::error_code ec;
    if (!fs::exists(dir)) return true;
    fs::remove_all(dir, ec);
    if (ec) { err = L"删除失败: " + dir + L" " + su_cvt(ec.message().c_str()); return false; }
    return true;
}

bool IsPathInside(const std::wstring& path, const std::wstring& directory)
{
    auto p = NormTrail(fs::weakly_canonical(fs::path(path)).wstring());
    auto d = NormTrail(fs::weakly_canonical(fs::path(directory)).wstring());
    // 统一大小写比较（Windows 不区分大小写）
    std::wstring pl = p, dl = d;
    std::transform(pl.begin(), pl.end(), pl.begin(), ::towlower);
    std::transform(dl.begin(), dl.end(), dl.begin(), ::towlower);
    return pl.rfind(dl, 0) == 0;
}

bool QqRunning()
{
    // 枚举名为 QQ.exe 的进程（用 CreateToolhelp32Snapshot，无额外依赖）
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"QQ.exe") == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

bool WriteFileAtomic(const std::wstring& target, const std::string& data, std::wstring& err)
{
    std::wstring tmp = target + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = L"无法创建临时文件: " + tmp + L" (err=" + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    DWORD written = 0;
    if (!WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) ||
        written != (DWORD)data.size()) {
        CloseHandle(h);
        DeleteFileW(tmp.c_str());
        err = L"写入失败: " + target;
        return false;
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        err = L"替换失败: " + target + L" (err=" + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    return true;
}

} // namespace fsutil

// 窄字符转宽（供 CopyDir 错误信息用）
std::wstring su_cvt(const char* s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}
