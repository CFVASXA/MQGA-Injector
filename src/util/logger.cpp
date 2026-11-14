// logger.cpp — 加锁追加写日志。
#include "logger.h"
#include "paths.h"
#include <windows.h>
#include <mutex>

namespace logger {

static std::mutex g_mutex;

void Write(const std::wstring& msg)
{
    paths::EnsureDirs();
    char timeBuf[32];
    __time64_t t = _time64(nullptr);
    struct tm tmv;
    localtime_s(&tmv, &t);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmv);

    // UTF-8 写入，便于跨工具查看
    int n = WideCharToMultiByte(CP_UTF8, 0, msg.data(), (int)msg.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, msg.data(), (int)msg.size(), u.data(), n, nullptr, nullptr);

    std::string line = "[" + std::string(timeBuf) + "] " + u + "\r\n";
    std::lock_guard<std::mutex> lk(g_mutex);
    HANDLE h = CreateFileW(paths::LogFile().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, line.data(), (DWORD)line.size(), &written, nullptr);
        CloseHandle(h);
    }
}

} // namespace logger
