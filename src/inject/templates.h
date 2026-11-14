#pragma once
#include <string>
#include <vector>
#include "asar.h"

namespace inject {
    // hostRoot: 宿主安装后的根目录；qqVersion: QQ 版本目录名；resources: QQ resources 目录。
    std::vector<AsarFile> BuildShadowFiles(const std::wstring& hostRoot,
                                           const std::wstring& qqVersion,
                                           const std::wstring& resources);
}
