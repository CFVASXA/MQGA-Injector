// factory.h — 模式工厂：根据配置创建对应注入模式
#pragma once
#include "../core/types.h"
#include "base.h"
#include <memory>

namespace modes {

// 创建模式实例。返回 nullptr 表示该模式不可用/未实现。
// 只返回实测可用的模式；不可用模式在此层被过滤掉，上层无需判断。
std::unique_ptr<InjectModeBase> CreateMode(core::InjectMode mode);

// 该模式当前是否可用（UI 用于灰显不可选模式）
bool IsModeAvailable(core::InjectMode mode);

// 模式的中文名（UI 显示）
const wchar_t* ModeDisplayName(core::InjectMode mode);

// 模式的实测状态说明（UI 显示，如"实测通过"）
const wchar_t* ModeStatusText(core::InjectMode mode);

} // namespace modes