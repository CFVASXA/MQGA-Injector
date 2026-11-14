// locate.h — QQ 安装目录与可用版本定位。
#pragma once
#include <string>
#include <vector>

namespace qq {
    // 找到 QQ.exe 所在根目录；找不到返回空串
    std::wstring FindQqRoot();
    // 列出 qqRoot\versions\ 下含 resources\app\application.asar 的版本目录名，按名称倒序
    std::vector<std::wstring> ListVersions(const std::wstring& qqRoot);
}
