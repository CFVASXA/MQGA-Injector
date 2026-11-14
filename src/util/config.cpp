// config.cpp — 配置读写。保存为原子（临时文件+替换），加载失败回默认并记日志。
#include "config.h"
#include "paths.h"
#include "strutil.h"
#include "logger.h"
#include <windows.h>
#include <fstream>
#include <sstream>

namespace config {

static std::string Serialize(const AppConfig& c)
{
    std::string s = "{\n";
    s += "  \"host\": " + su::JsonStr(c.host) + ",\n";
    s += "  \"hostSourceDir\": " + su::JsonStr(c.hostSourceDir) + ",\n";
    s += "  \"installDir\": " + su::JsonStr(c.installDir) + ",\n";
    s += "  \"qqRoot\": " + su::JsonStr(c.qqRoot) + ",\n";
    s += "  \"version\": " + su::JsonStr(c.version) + "\n";
    s += "}";
    return s;
}

static AppConfig Deserialize(const std::string& utf8)
{
    AppConfig c;
    auto m = su::JsonParseFlat(utf8);
    auto get = [&](const wchar_t* k, std::wstring& dst) {
        auto it = m.find(k);
        if (it != m.end()) dst = it->second;
    };
    get(L"host", c.host);
    get(L"hostSourceDir", c.hostSourceDir);
    get(L"installDir", c.installDir);
    get(L"qqRoot", c.qqRoot);
    get(L"version", c.version);
    return c;
}

AppConfig Load()
{
    std::ifstream f(paths::ConfigFile(), std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        return Deserialize(ss.str());
    } catch (...) {
        logger::Write(L"配置解析失败，使用默认值");
        return {};
    }
}

bool Save(const AppConfig& c)
{
    paths::EnsureDirs();
    std::wstring tmp = paths::ConfigFile() + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << Serialize(c);
        if (!f.good()) return false;
    }
    if (!MoveFileExW(tmp.c_str(), paths::ConfigFile().c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        logger::Write(L"配置保存失败: MoveFileExW err=" + std::to_wstring(GetLastError()));
        return false;
    }
    return true;
}

} // namespace config
