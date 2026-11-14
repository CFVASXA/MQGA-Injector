// fsutil.h — 文件系统辅助：目录复制/删除、路径包含判断、进程检查。
#pragma once
#include <string>

// 窄字符转宽字符（错误信息用）
std::wstring su_cvt(const char* s);

namespace fsutil {
    bool CopyDir(const std::wstring& src, const std::wstring& dst, std::wstring& err);
    bool RemoveDirIfExists(const std::wstring& dir, std::wstring& err);
    bool FileExists(const std::wstring& p);
    bool DirExists(const std::wstring& p);
    // path 是否在 directory 内部（精确前缀，带分隔符边界；不区分大小写）
    bool IsPathInside(const std::wstring& path, const std::wstring& directory);
    bool QqRunning();
    bool WriteFileAtomic(const std::wstring& target, const std::string& utf8OrBytes, std::wstring& err);
}
