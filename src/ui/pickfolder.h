// pickfolder.h — 目录选择对话框（IFileDialog，CLSID 直启，不依赖 shell32.lib）。
#pragma once
#include <string>

namespace ui {
    // 弹出目录选择对话框；返回用户选择的目录（取消返回空串）。
    // title 为对话框标题；initial 为初始目录（可空）。
    std::wstring PickFolder(const std::wstring& title, const std::wstring& initial);
}
