// paths.cpp — 路径实现。全部基于 exe 真实位置，与工作目录无关。
#include "paths.h"
#include <windows.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace paths {

static std::wstring EnsureTrail(std::wstring s)
{
    if (!s.empty() && s.back() != L'\\') s += L'\\';
    return s;
}

std::wstring ExeDir()
{
    static std::wstring cached = [] {
        wchar_t buf[MAX_PATH]{};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return EnsureTrail(fs::path(buf).parent_path().wstring());
    }();
    return cached;
}

std::wstring DataDir()      { return EnsureTrail(ExeDir() + L"data"); }
std::wstring ConfigFile()   { return DataDir() + L"config.json"; }
std::wstring LogFile()      { return DataDir() + L"install.log"; }
std::wstring DownloadDir()  { return EnsureTrail(DataDir() + L"download"); }
std::wstring HostsDir()     { return EnsureTrail(DataDir() + L"hosts"); }
std::wstring WebDir()       { return EnsureTrail(ExeDir() + L"web"); }

void EnsureDirs()
{
    static bool done = false;
    if (done) return;
    std::error_code ec;
    fs::create_directories(DataDir(), ec);
    fs::create_directories(DownloadDir(), ec);
    fs::create_directories(HostsDir(), ec);
    done = true;
}

} // namespace paths
