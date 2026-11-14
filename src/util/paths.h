// paths.h — 程序目录与数据目录路径。所有路径相关计算唯一入口。
#pragma once
#include <string>

namespace paths {
    std::wstring ExeDir();        // exe 所在目录（含尾部反斜杠）
    std::wstring DataDir();       // ExeDir + "data\"
    std::wstring ConfigFile();    // DataDir + "config.json"
    std::wstring LogFile();       // DataDir + "install.log"
    std::wstring DownloadDir();   // DataDir + "download\"
    std::wstring HostsDir();      // DataDir + "hosts\"
    std::wstring WebDir();        // ExeDir + "web\"
    void EnsureDirs();            // 幂等创建全部目录
}
