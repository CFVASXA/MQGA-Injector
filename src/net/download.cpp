#include "net/download.h"
#include "net/http.h"
#include "net/unzip.h"
#include "util/paths.h"
#include "util/logger.h"
#include "util/fsutil.h"
#include <windows.h>
#include <array>
#include <string>
#include <ctime>
#include <chrono>

namespace net {

int DownloadHost(const std::wstring& hostKey,
                 std::function<void(int percent, const std::wstring& msg)> progress,
                 std::wstring& err) {
    err.clear();
    std::wstring repository;
    if (hostKey == L"mqga") repository = L"Simon404Error/MakeQQGreatAgain";
    else if (hostKey == L"liteloader") repository = L"LiteLoaderQQNT/LiteLoaderQQNT";
    else {
        err = L"Unknown host key: " + hostKey;
        return 1;
    }

    paths::EnsureDirs();
    const std::wstring target = L"https://github.com/" + repository + L"/archive/refs/heads/main.zip";
    const std::array<std::wstring, 3> mirrors = {
        L"https://gh-proxy.com/", L"https://ghproxy.net/", L""
    };
    const std::wstring savePath = paths::DownloadDir() + hostKey + L"-main.zip";
    int lastPercent = -1;

    for (size_t index = 0; index < mirrors.size(); ++index) {
        const std::wstring url = mirrors[index] + target;
        const std::wstring label = mirrors[index].empty() ? L"direct" : mirrors[index];
        if (progress) progress(0, L"Trying " + label);
        logger::Write(L"Download host=" + hostKey + L" source=" + label);
        bool ok = HttpDownloadToFile(url, savePath,
            [&](long long got, long long total) {
                int percent = 0;
                if (total > 0) {
                    long long value = got * 100 / total;
                    percent = static_cast<int>(value < 0 ? 0 : (value > 100 ? 100 : value));
                } else if (got > 0) {
                    percent = 99;
                }
                if (percent != lastPercent && progress) {
                    progress(percent, L"Downloading via " + label);
                    lastPercent = percent;
                }
            }, err);
        if (ok) {
            if (progress) progress(100, L"Downloaded via " + label);
            logger::Write(L"Download succeeded source=" + label + L" file=" + savePath);
            return 0;
        }
        DeleteFileW(savePath.c_str());
        DeleteFileW((savePath + L".part").c_str());
        logger::Write(L"Download failed source=" + label + L": " + err);
        if (progress) progress(0, L"Failed " + label + L": " + err);
    }
    return 2;
}

std::wstring ExtractHost(const std::wstring& hostKey, std::wstring& err)
{
    paths::EnsureDirs();
    const std::wstring zipPath = paths::DownloadDir() + hostKey + L"-main.zip";
    HANDLE h = CreateFileW(zipPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = L"zip not found: " + zipPath; return L""; }
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    std::string bytes((size_t)sz.QuadPart, '\0');
    DWORD read = 0;
    ReadFile(h, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
    CloseHandle(h);

    const std::wstring tempDir = paths::DownloadDir() + hostKey + L"-extracting";
    std::wstring rmErr;
    fsutil::RemoveDirIfExists(tempDir, rmErr);
    if (!ZipExtract(bytes, tempDir, err)) return L"";

    // GitHub zip 顶层是 repo-main/ 包装目录；找含 src\main.js 的顶层目录
    std::wstring top;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((tempDir + L"\\*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) {
                std::wstring cand = tempDir + L"\\" + fd.cFileName;
                if (fsutil::FileExists(cand + L"\\src\\main.js")) { top = cand; break; }
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    if (top.empty() && fsutil::FileExists(tempDir + L"\\src\\main.js")) {
        top = tempDir; // zip 无包装目录
    }
    if (top.empty()) {
        fsutil::RemoveDirIfExists(tempDir, rmErr);
        err = L"src\\main.js not found in archive";
        return L"";
    }

    const std::wstring destRoot = paths::HostsDir() + hostKey;
    if (fsutil::DirExists(destRoot)) {
        const std::wstring backup = destRoot + L"-backup-" +
            std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        MoveFileW(destRoot.c_str(), backup.c_str());
        logger::Write(L"Old host backed up: " + backup);
    }

    if (top == tempDir) {
        if (!MoveFileW(tempDir.c_str(), destRoot.c_str())) {
            err = L"Move failed (err=" + std::to_wstring(GetLastError()) + L")";
            return L"";
        }
    } else {
        if (!MoveFileW(top.c_str(), destRoot.c_str())) {
            err = L"Move failed (err=" + std::to_wstring(GetLastError()) + L")";
            return L"";
        }
        fsutil::RemoveDirIfExists(tempDir, rmErr);
    }
    logger::Write(L"Host ready: " + destRoot);
    return destRoot;
}

} // namespace net
