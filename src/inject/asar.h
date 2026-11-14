#pragma once
#include <string>
#include <vector>

namespace inject {
    struct AsarFile { std::string path; std::string bytes; }; // path 用 / 分隔

    // 生成 ASAR 到内存：外层 Pickle、JSON 头、连续文件体。
    std::string AsarPack(const std::vector<AsarFile>& files, std::wstring& err);
}
