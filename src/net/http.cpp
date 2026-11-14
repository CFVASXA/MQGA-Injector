// WinHTTP is resolved at run time so this module needs no winhttp import library.
#include "net/http.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>

namespace net {
namespace {

using HInternet = HINTERNET;
using OpenFn = HInternet (WINAPI *)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
using ConnectFn = HInternet (WINAPI *)(HInternet, LPCWSTR, INTERNET_PORT, DWORD);
using OpenRequestFn = HInternet (WINAPI *)(HInternet, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
using SendRequestFn = BOOL (WINAPI *)(HInternet, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
using ReceiveResponseFn = BOOL (WINAPI *)(HInternet, LPVOID);
using QueryDataAvailableFn = BOOL (WINAPI *)(HInternet, LPDWORD, LPVOID);
using ReadDataFn = BOOL (WINAPI *)(HInternet, LPVOID, DWORD, LPDWORD);
using CloseHandleFn = BOOL (WINAPI *)(HInternet);
using SetOptionFn = BOOL (WINAPI *)(HInternet, DWORD, LPVOID, DWORD);
using SetTimeoutsFn = BOOL (WINAPI *)(HInternet, int, int, int, int);
using QueryHeadersFn = BOOL (WINAPI *)(HInternet, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
using CrackUrlFn = BOOL (WINAPI *)(LPCWSTR, DWORD, DWORD, LPURL_COMPONENTSW);

struct WinHttpApi {
    HMODULE module = nullptr;
    OpenFn open = nullptr;
    ConnectFn connect = nullptr;
    OpenRequestFn openRequest = nullptr;
    SendRequestFn sendRequest = nullptr;
    ReceiveResponseFn receiveResponse = nullptr;
    QueryDataAvailableFn queryDataAvailable = nullptr;
    ReadDataFn readData = nullptr;
    CloseHandleFn closeHandle = nullptr;
    SetOptionFn setOption = nullptr;
    SetTimeoutsFn setTimeouts = nullptr;
    QueryHeadersFn queryHeaders = nullptr;
    CrackUrlFn crackUrl = nullptr;

    ~WinHttpApi() {
        if (module) FreeLibrary(module);
    }
};

std::wstring ErrorMessage(DWORD code) {
    wchar_t* message = nullptr;
    DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = L"Windows error " + std::to_wstring(code);
    if (size && message) {
        result.assign(message, size);
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' ||
                                   result.back() == L' ')) result.pop_back();
        result += L" (" + std::to_wstring(code) + L")";
    }
    if (message) LocalFree(message);
    return result;
}

template<typename T>
bool Resolve(HMODULE module, const char* name, T& target) {
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    return target != nullptr;
}

bool LoadApi(WinHttpApi& api, std::wstring& err) {
    api.module = LoadLibraryW(L"winhttp.dll");
    if (!api.module) {
        err = L"LoadLibraryW(winhttp.dll): " + ErrorMessage(GetLastError());
        return false;
    }
    if (!Resolve(api.module, "WinHttpOpen", api.open) ||
        !Resolve(api.module, "WinHttpConnect", api.connect) ||
        !Resolve(api.module, "WinHttpOpenRequest", api.openRequest) ||
        !Resolve(api.module, "WinHttpSendRequest", api.sendRequest) ||
        !Resolve(api.module, "WinHttpReceiveResponse", api.receiveResponse) ||
        !Resolve(api.module, "WinHttpQueryDataAvailable", api.queryDataAvailable) ||
        !Resolve(api.module, "WinHttpReadData", api.readData) ||
        !Resolve(api.module, "WinHttpCloseHandle", api.closeHandle) ||
        !Resolve(api.module, "WinHttpSetOption", api.setOption) ||
        !Resolve(api.module, "WinHttpSetTimeouts", api.setTimeouts) ||
        !Resolve(api.module, "WinHttpQueryHeaders", api.queryHeaders) ||
        !Resolve(api.module, "WinHttpCrackUrl", api.crackUrl)) {
        err = L"GetProcAddress: a required WinHTTP function is unavailable";
        return false;
    }
    return true;
}

bool ParseUrl(const WinHttpApi& api, const std::wstring& url,
              std::wstring& host, std::wstring& path,
              INTERNET_PORT& port, bool& secure, std::wstring& err) {
    // Two buffers own the storage referenced by the URL components.
    std::vector<wchar_t> hostBuffer(url.size() + 1);
    std::vector<wchar_t> pathBuffer(url.size() + 1);
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = hostBuffer.data();
    parts.dwHostNameLength = static_cast<DWORD>(hostBuffer.size());
    parts.lpszUrlPath = pathBuffer.data();
    parts.dwUrlPathLength = static_cast<DWORD>(pathBuffer.size());
    if (!api.crackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
        err = L"Invalid URL: " + url + L": " + ErrorMessage(GetLastError());
        return false;
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) {
        err = L"Only HTTP and HTTPS URLs are supported";
        return false;
    }
    host.assign(parts.lpszHostName, parts.dwHostNameLength);
    path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    // Preserve a query string: WinHttpCrackUrl deliberately separates it from the path.
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    port = parts.nPort;
    secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

bool QueryHeader(const WinHttpApi& api, HInternet request, DWORD flag,
                 std::wstring& value) {
    // 首次查询拿真实字节大小（Content-Length / Location 可能远超固定缓冲）
    DWORD size = 0;
    if (!api.queryHeaders(request, flag, WINHTTP_HEADER_NAME_BY_INDEX,
                          WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    }
    if (size == 0) return false;
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (!api.queryHeaders(request, flag, WINHTTP_HEADER_NAME_BY_INDEX,
                          buffer.data(), &size, WINHTTP_NO_HEADER_INDEX)) return false;
    value.assign(buffer.data());
    return true;
}

bool IsRedirect(DWORD status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool DownloadOnce(const WinHttpApi& api, const std::wstring& url,
                  const std::wstring& savePath,
                  const std::function<void(long long, long long)>& progress,
                  std::wstring& redirectUrl, std::wstring& err) {
    std::wstring host, path;
    INTERNET_PORT port = 0;
    bool secure = false;
    if (!ParseUrl(api, url, host, path, port, secure, err)) return false;

    HInternet session = api.open(L"MQGA-Injector/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        err = L"WinHttpOpen: " + ErrorMessage(GetLastError());
        return false;
    }
    HInternet connection = nullptr;
    HInternet request = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    const DWORD timeout = 10 * 60 * 1000;
    api.setTimeouts(session, timeout, timeout, timeout, timeout);
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    api.setOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    bool success = false;

    do {
        connection = api.connect(session, host.c_str(), port, 0);
        if (!connection) {
            err = L"WinHttpConnect " + host + L": " + ErrorMessage(GetLastError());
            break;
        }
        request = api.openRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  secure ? WINHTTP_FLAG_SECURE : 0);
        if (!request) {
            err = L"WinHttpOpenRequest: " + ErrorMessage(GetLastError());
            break;
        }
        if (!api.sendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                             0, 0, 0)) {
            err = L"WinHttpSendRequest: " + ErrorMessage(GetLastError());
            break;
        }
        if (!api.receiveResponse(request, nullptr)) {
            err = L"WinHttpReceiveResponse: " + ErrorMessage(GetLastError());
            break;
        }
        std::wstring value;
        if (!QueryHeader(api, request, WINHTTP_QUERY_STATUS_CODE, value)) {
            err = L"WinHttpQueryHeaders(status): " + ErrorMessage(GetLastError());
            break;
        }
        DWORD status = static_cast<DWORD>(wcstoul(value.c_str(), nullptr, 10));
        if (IsRedirect(status)) {
            if (!QueryHeader(api, request, WINHTTP_QUERY_LOCATION, redirectUrl)) {
                err = L"HTTP redirect has no Location header";
                break;
            }
            if (redirectUrl.compare(0, 7, L"http://") != 0 &&
                redirectUrl.compare(0, 8, L"https://") != 0) {
                err = L"Unsupported relative HTTP redirect";
                break;
            }
            success = true; // Redirect handling belongs to the caller; no file was opened.
            break;
        }
        if (status != 200) {
            err = L"HTTP status " + std::to_wstring(status) + L" for " + url;
            break;
        }

        long long total = 0;
        if (QueryHeader(api, request, WINHTTP_QUERY_CONTENT_LENGTH, value))
            total = _wcstoi64(value.c_str(), nullptr, 10);
        // Use a sibling temporary file so a failed transfer cannot leave a convincing zip.
        std::wstring temporary = savePath + L".part";
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            err = L"CreateFileW " + temporary + L": " + ErrorMessage(GetLastError());
            break;
        }
        long long received = 0;
        if (progress) progress(0, total);
        for (;;) {
            DWORD available = 0;
            if (!api.queryDataAvailable(request, &available, nullptr)) {
                err = L"WinHttpQueryDataAvailable: " + ErrorMessage(GetLastError());
                break;
            }
            if (available == 0) { success = true; break; }
            std::vector<char> buffer(available);
            DWORD read = 0;
            if (!api.readData(request, buffer.data(), available, &read)) {
                err = L"WinHttpReadData: " + ErrorMessage(GetLastError());
                break;
            }
            if (read == 0) { success = true; break; }
            DWORD written = 0;
            if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
                err = L"WriteFile " + temporary + L": " + ErrorMessage(GetLastError());
                break;
            }
            received += read;
            if (progress) progress(received, total);
        }
        if (!success) break;
        if (!FlushFileBuffers(file)) {
            err = L"FlushFileBuffers: " + ErrorMessage(GetLastError());
            success = false;
            break;
        }
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(temporary.c_str(), savePath.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            err = L"MoveFileExW: " + ErrorMessage(GetLastError());
            success = false;
            break;
        }
    } while (false);

    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!success) DeleteFileW((savePath + L".part").c_str());
    if (request) api.closeHandle(request);
    if (connection) api.closeHandle(connection);
    api.closeHandle(session);
    return success;
}

} // namespace

bool HttpDownloadToFile(const std::wstring& url, const std::wstring& savePath,
                        std::function<void(long long got, long long total)> progress,
                        std::wstring& err) {
    err.clear();
    WinHttpApi api;
    if (!LoadApi(api, err)) return false;
    std::wstring current = url;
    DeleteFileW((savePath + L".part").c_str());
    for (int redirect = 0; redirect <= 10; ++redirect) {
        std::wstring next;
        if (!DownloadOnce(api, current, savePath, progress, next, err)) return false;
        if (next.empty()) return true;
        if (redirect == 10) {
            err = L"Too many HTTP redirects";
            return false;
        }
        current = next;
    }
    return false;
}

} // namespace net
