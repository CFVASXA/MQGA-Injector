#pragma once
#include <string>
#include <functional>

namespace net {
    // hostKey: "mqga" 或 "liteloader"
    // 镜像链: https://gh-proxy.com/ 前缀 -> https://ghproxy.net/ 前缀 -> 无前缀直连
    // 目标: https://github.com/{repo}/archive/refs/heads/main.zip
    // 返回 0 成功；非 0 失败，err 给原因。progress 为 0-100。
    int DownloadHost(const std::wstring& hostKey,
                     std::function<void(int percent, const std::wstring& msg)> progress,
                     std::wstring& err);

    // 解压 DownloadDir\{hostKey}-main.zip 到 HostsDir\{hostKey}
    // 返回源码根目录（含顶层 repo-main 剥离）；失败返回空串，err 给原因
    std::wstring ExtractHost(const std::wstring& hostKey, std::wstring& err);
}
