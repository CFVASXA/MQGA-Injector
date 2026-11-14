// config.h — 配置读写（原子保存）。
#pragma once
#include <string>

struct AppConfig {
    std::wstring host          = L"mqga";
    std::wstring hostSourceDir = L"";
    std::wstring installDir    = L"D:\\LiteLoaderQQNT";
    std::wstring qqRoot        = L"";
    std::wstring version       = L"";

    bool operator==(const AppConfig& o) const {
        return host == o.host && hostSourceDir == o.hostSourceDir &&
               installDir == o.installDir && qqRoot == o.qqRoot && version == o.version;
    }
};

namespace config {
    AppConfig Load();
    bool Save(const AppConfig& c);
}
