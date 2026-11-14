#pragma once
#include <string>
#include <functional>

namespace net {
    // HTTPS GET 下载到文件；progress 回调参数为已下载字节数与总字节数(总长未知时为0)。
    // 返回 true 成功。失败时 err 给出原因。
    bool HttpDownloadToFile(const std::wstring& url,
                            const std::wstring& savePath,
                            std::function<void(long long got, long long total)> progress,
                            std::wstring& err);
}
