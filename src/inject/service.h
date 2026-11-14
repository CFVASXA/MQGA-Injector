// service.h — 注入服务（支持多模式：app.asar 替换 / MinHook 内存 hook）
#pragma once
#include <string>
#include "../util/config.h"
#include <vector>

namespace inject {

struct InjectResult {
    bool ok;
    std::wstring message;
};

enum class InjectMode {
    AsarReplace,    // 替换 app.asar（当前方案，稳定）
    MinHook,        // MinHook 内存 hook（新方案，不修改文件）
    // 未来扩展：ProxyDll, MemoryPatch, ...
};

// AppConfig 在 util/config.h 里定义（全局命名空间）

// 注入（根据 mode 选择实现）
InjectResult DoInstall(const AppConfig& c);
InjectResult DoDisable(const AppConfig& c);
InjectResult DoEnable(const AppConfig& c);
InjectResult DoUninstall(const AppConfig& c);

// 获取宿主路径（用于 MinHook 模式）
std::wstring GetHostPath(const std::wstring& hostKey);

} // namespace inject
