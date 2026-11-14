// locate.cpp — 通过卸载注册表和常见安装目录定位 QQNT。
#include "locate.h"
#include "util/fsutil.h"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace qq {

namespace {

const wchar_t* kUninstallKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\QQ";

bool IsSpace(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

void Trim(std::wstring& s)
{
    size_t first = 0;
    while (first < s.size() && IsSpace(s[first])) ++first;
    size_t last = s.size();
    while (last > first && IsSpace(s[last - 1])) --last;
    s = s.substr(first, last - first);
}

// DisplayIcon 的图标索引形如 QQ.exe,0，只移除末尾的数字后缀。
void RemoveIconIndex(std::wstring& s)
{
    Trim(s);
    size_t comma = s.rfind(L',');
    if (comma == std::wstring::npos || comma + 1 == s.size()) return;
    for (size_t i = comma + 1; i < s.size(); ++i) {
        if (s[i] < L'0' || s[i] > L'9') return;
    }
    s.erase(comma);
    Trim(s);
}

std::wstring ReadDisplayIconView(HKEY hive, REGSAM access);
std::wstring ParseDisplayIcon(std::wstring value)
{
    Trim(value);
    if (value.empty()) return L"";

    // 同时兼容 "D:\QQNT\QQ.exe",0 和 "D:\QQNT\QQ.exe,0"。
    if (value.front() == L'"') {
        size_t quote = value.find(L'"', 1);
        value = quote == std::wstring::npos ? value.substr(1) : value.substr(1, quote - 1);
    } else if (value.back() == L'"') {
        value.pop_back();
    }
    RemoveIconIndex(value);
    return value;
}

std::wstring ReadDisplayIcon(HKEY hive)
{
    // 先试 64 位视图，再试 WOW6432Node（32 位安装器常写在那里）。
    std::wstring v = ReadDisplayIconView(hive, KEY_READ);
    if (v.empty()) v = ReadDisplayIconView(hive, KEY_READ | KEY_WOW64_32KEY);
    return v;
}

std::wstring ReadDisplayIconView(HKEY hive, REGSAM access)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(hive, kUninstallKey, 0, access, &key) != ERROR_SUCCESS) return L"";

    std::wstring result;
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, L"DisplayIcon", nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && bytes > 0) {
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(key, L"DisplayIcon", nullptr, &type,
                             reinterpret_cast<LPBYTE>(buffer.data()), &bytes) == ERROR_SUCCESS) {
            result.assign(buffer.data());
            if (type == REG_EXPAND_SZ) {
                DWORD size = ExpandEnvironmentStringsW(result.c_str(), nullptr, 0);
                if (size > 1) {
                    std::vector<wchar_t> expanded(size, L'\0');
                    if (ExpandEnvironmentStringsW(result.c_str(), expanded.data(), size) == size)
                        result.assign(expanded.data());
                }
            }
        }
    }
    RegCloseKey(key);
    return ParseDisplayIcon(result);
}

} // anonymous namespace

std::wstring FindQqRoot()
{
    try {
        std::vector<std::wstring> candidates;
        for (HKEY hive : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
            std::wstring iconPath = ReadDisplayIcon(hive);
            if (!iconPath.empty()) candidates.push_back(fs::path(iconPath).parent_path().wstring());
        }

        candidates.emplace_back(L"D:\\QQNT");
        candidates.emplace_back(L"C:\\Program Files\\Tencent\\QQNT");
        candidates.emplace_back(L"C:\\Program Files (x86)\\Tencent\\QQNT");

        // 最后使用实际 ProgramFiles 环境变量，兼容非 C 盘安装。
        DWORD size = GetEnvironmentVariableW(L"ProgramFiles", nullptr, 0);
        if (size > 1) {
            std::vector<wchar_t> buffer(size, L'\0');
            if (GetEnvironmentVariableW(L"ProgramFiles", buffer.data(), size) == size - 1)
                candidates.push_back((fs::path(buffer.data()) / L"Tencent" / L"QQNT").wstring());
        }

        for (const auto& root : candidates) {
            if (!root.empty() && fsutil::FileExists((fs::path(root) / L"QQ.exe").wstring())) return root;
        }
    } catch (...) {
        // 注册表内容异常或路径无效时静默回退。
    }
    return L"";
}

std::vector<std::wstring> ListVersions(const std::wstring& qqRoot)
{
    std::vector<std::wstring> versions;
    try {
        std::error_code ec;
        fs::directory_iterator it(fs::path(qqRoot) / L"versions", ec);
        const fs::directory_iterator end;
        while (!ec && it != end) {
            if (it->is_directory(ec) && !ec) {
                fs::path asar = it->path() / L"resources" / L"app" / L"application.asar";
                if (fs::is_regular_file(asar, ec) && !ec)
                    versions.push_back(it->path().filename().wstring());
            }
            ec.clear();
            it.increment(ec);
        }
        std::sort(versions.begin(), versions.end(),
                  [](const std::wstring& a, const std::wstring& b) { return a > b; });
    } catch (...) {
        // 无权限、路径无效等情况按无可用版本处理。
        versions.clear();
    }
    return versions;
}

} // namespace qq
