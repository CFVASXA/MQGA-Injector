// service.cpp — 注入服务门面实现（分派到 modes 层）
// 这里是唯一的模式分派点。新增模式只需在 factory.cpp 注册，此处不用改。
#include "service.h"

#include "../modes/factory.h"
#include "../util/fsutil.h"
#include "../util/logger.h"
#include "../util/paths.h"

#include <filesystem>

namespace inject {
namespace {

namespace fs = std::filesystem;

core::InjectResult NotSupported(const wchar_t* what) {
    std::wstring msg = std::wstring(L"当前模式不支持") + what;
    logger::Write(L"提示: " + msg);
    return {false, msg};
}

// 取模式实例；为空时返回明确错误（不调用者判断）
core::InjectResult GetMode(const core::AppConfig& c,
                           std::unique_ptr<modes::InjectModeBase>& out) {
    auto mode = modes::CreateMode(c.mode);
    if (!mode) {
        std::wstring msg = std::wstring(L"模式不可用: ") +
                           modes::ModeDisplayName(c.mode) + L"（" +
                           modes::ModeStatusText(c.mode) + L"）";
        logger::Write(msg);
        return {false, msg};
    }
    out = std::move(mode);
    return {true, L""};
}

} // namespace

core::InjectResult Install(const core::AppConfig& c) {
    std::unique_ptr<modes::InjectModeBase> mode;
    auto g = GetMode(c, mode);
    if (!g.ok) return g;
    return mode->Install(c);
}

core::InjectResult Uninstall(const core::AppConfig& c) {
    std::unique_ptr<modes::InjectModeBase> mode;
    auto g = GetMode(c, mode);
    if (!g.ok) return g;
    return mode->Uninstall(c);
}

core::InjectResult Disable(const core::AppConfig& c) {
    std::unique_ptr<modes::InjectModeBase> mode;
    auto g = GetMode(c, mode);
    if (!g.ok) return g;
    return mode->Disable(c);
}

core::InjectResult Enable(const core::AppConfig& c) {
    std::unique_ptr<modes::InjectModeBase> mode;
    auto g = GetMode(c, mode);
    if (!g.ok) return g;
    return mode->Enable(c);
}

std::wstring GetHostPath(const std::wstring& hostKey) {
    const std::wstring path = (fs::path(paths::HostsDir()) / hostKey).wstring();
    return fsutil::DirExists(path) ? path : L"";
}

} // namespace inject