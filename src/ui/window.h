// window.h — Win32 主窗口 + WebView2 宿主。
#pragma once
#include <string>

namespace ui {
    // 创建窗口、初始化 WebView2、加载 web\index.html、挂消息桥。
    // 运行消息循环直到窗口关闭。返回退出码。
    int Run();
}
