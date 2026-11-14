#pragma once
#include <string>
#include "../util/config.h"

namespace inject {
    struct InjectResult { bool ok; std::wstring message; };

    InjectResult DoInstall(const AppConfig& c);
    InjectResult DoDisable(const AppConfig& c);
    InjectResult DoEnable(const AppConfig& c);
    InjectResult DoUninstall(const AppConfig& c);

    // 已下载宿主在 paths::HostsDir()\hostKey；返回其路径，不存在返回空。
    std::wstring GetHostPath(const std::wstring& hostKey);
}
