// bridge.h — WebView2 ↔ C++ 消息分发。每个 action 一个处理函数。
#pragma once
#include <string>

namespace ui {
    // 收到 JS 消息（UTF-8 JSON 文本）时由 window 层调用。
    // 处理后通过 PostJsonToJs 回发结果。
    using PostFn = void(*)(const std::wstring& utf8Json);
    void SetPostFn(PostFn fn);
    void HandleMessage(const std::string& utf8Json);
}
