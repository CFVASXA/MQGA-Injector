// manualmap.h — Manual Map 手动映射模式
#pragma once
#include "base.h"

namespace modes {

class ManualMapMode : public InjectModeBase {
public:
    core::InjectResult Install(const core::AppConfig& c) override;
    const wchar_t* Name() const override { return L"Manual Map 手动映射"; }
};

} // namespace modes