// logger.h — 线程安全日志。
#pragma once
#include <string>

namespace logger {
    void Write(const std::wstring& msg);
}
