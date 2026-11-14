// types.h — 基础类型定义（地基：所有模块依赖此）
#pragma once
#include <string>

namespace core {

// 注入结果（统一返回类型）
struct InjectResult {
    bool ok;
    std::wstring message;
};

// 注入模式（可扩展）
enum class InjectMode {
    AsarReplace,    // app.asar 替换（稳定，实测通过）
    MinHook,        // MinHook 内存注入（QQ 有反注入保护）
    ManualMap,      // Manual Map 手动映射（绕过 LoadLibrary，实测成功）
    Ifeo,           // IFEO 调试器注入（触发成功但循环待解）
    ProxyDll,       // 代理 ffmpeg.dll（函数签名复杂，实测报"无法找到入口"）
    EnvInject,      // 环境变量注入（实测无效，QQ 不走 NODE_OPTIONS）
};

// 应用配置（全局唯一）
struct AppConfig {
    std::wstring host          = L"mqga";
    std::wstring hostSourceDir = L"";
    std::wstring installDir    = L"D:\\LiteLoaderQQNT";
    std::wstring qqRoot        = L"";
    std::wstring version       = L"";
    InjectMode mode            = InjectMode::AsarReplace;

    bool operator==(const AppConfig& o) const {
        return host == o.host && hostSourceDir == o.hostSourceDir &&
               installDir == o.installDir && qqRoot == o.qqRoot &&
               version == o.version && mode == o.mode;
    }
};

} // namespace core
