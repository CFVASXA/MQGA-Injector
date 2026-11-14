// config.h — 配置读写（原子保存）。
#pragma once
#include <string>

// 注入模式（在 service.h 里定义，这里前置声明避免循环依赖）
namespace inject { enum class InjectMode; }

struct AppConfig {
    std::wstring host          = L"mqga";
    std::wstring hostSourceDir = L"";
    std::wstring installDir    = L"D:\LiteLoaderQQNT";
    std::wstring qqRoot        = L"";
    std::wstring version       = L"";
    // mode 在 service.h 里定义（避免循环依赖，实际存储为字符串）
    std::wstring mode          = L"asar";  // asar / minhook / ...

    bool operator==(const AppConfig& o) const {
        return host == o.host && hostSourceDir == o.hostSourceDir &&
               installDir == o.installDir && qqRoot == o.qqRoot && 
               version == o.version && mode == o.mode;
    }
};

namespace config {
    AppConfig Load();
    bool Save(const AppConfig& c);
}
