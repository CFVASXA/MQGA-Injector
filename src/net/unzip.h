#pragma once
#include <string>
#include <vector>

namespace net {
    struct ZipEntry {
        std::string name;
        unsigned long offset;
        unsigned long compressedSize;
        unsigned method;
    };

    // 列出 zip 中央目录条目
    std::vector<ZipEntry> ZipList(const std::string& bytes);
    // 解压 zip 字节流到目标目录（会创建目录结构）
    // 只支持 store(0) 和 deflate(8)；deflate 用自实现的 puff 解压器（RFC1951）
    // 防路径穿越：条目名含 ".." 或以 / 开头的直接跳过
    bool ZipExtract(const std::string& bytes, const std::wstring& destDir, std::wstring& err);
}
