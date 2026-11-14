// window.cpp — Win32 窗口 + WebView2（动态加载 WebView2Loader，无需 .lib）。
#include "window.h"
#include "bridge.h"
#include "../util/paths.h"
#include "../util/strutil.h"
#include "../util/logger.h"
#include <windows.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wrl.h>
#include <string>
#include <queue>
#include <mutex>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace ui {

static HWND g_hwnd = nullptr;
static ComPtr<ICoreWebView2> g_webview;
static ComPtr<ICoreWebView2Controller> g_controller;

// WebView2 COM 对象只能在创建它的 UI 线程使用。
// 后台线程通过队列 + PostMessage 投递到 UI 线程执行。
static constexpr UINT WM_APP_POSTJSON = WM_APP + 0x4D47; // 'MG'
static std::mutex g_jsonQueueMutex;
static std::queue<std::wstring> g_jsonQueue;

// ---- JS 回发：任意线程调用，排队到 UI 线程 ----
static void PostToJs(const std::wstring& utf8Json)
{
    {
        std::lock_guard<std::mutex> lk(g_jsonQueueMutex);
        g_jsonQueue.push(utf8Json);
    }
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_POSTJSON, 0, 0);
}

static void DrainJsonQueue()
{
    // 在 UI 线程执行：把队列里的消息逐一发给 WebView
    for (;;) {
        std::wstring item;
        {
            std::lock_guard<std::mutex> lk(g_jsonQueueMutex);
            if (g_jsonQueue.empty()) return;
            item = std::move(g_jsonQueue.front());
            g_jsonQueue.pop();
        }
        if (g_webview) g_webview->PostWebMessageAsJson(item.c_str());
    }
}

// ---- 窗口过程 ----
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_APP_POSTJSON:
        DrainJsonQueue();
        return 0;
    case WM_SIZE:
        if (g_controller) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            g_controller->put_Bounds(rc);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static std::wstring ExeDirOfWeb()
{
    return paths::WebDir();
}

int Run()
{
        paths::EnsureDirs();

        WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MQGAInjectorWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        if (!RegisterClassExW(&wc)) { logger::Write(L"Run: RegisterClass failed"); return 1; }
    
    g_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"MQGA 注入器",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 840,
        nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_hwnd) { logger::Write(L"Run: CreateWindow failed"); return 2; }
    ShowWindow(g_hwnd, SW_SHOW);

    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    if (!loader) {
        std::wstring p1 = paths::ExeDir() + L"WebView2Loader.dll";
        logger::Write(L"loader fallback: " + p1);
        loader = LoadLibraryW(p1.c_str());
    }
    if (!loader) {
        logger::Write(L"Run: loader try3");
        loader = LoadLibraryW(L"C:\\Windows\\System32\\WebView2Loader.dll");
    }
    if (!loader) {
        logger::Write(L"Run: loader not found");
        MessageBoxW(g_hwnd, L"找不到 WebView2Loader.dll", L"错误", MB_ICONERROR);
        return 3;
    }

    // 真实导出签名是 4 个参数：browserExecutableFolder, userDataFolder, options, handler
    typedef HRESULT(WINAPI* CreateEnvFn)(
        PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto createEnv = (CreateEnvFn)GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions");
    if (!createEnv) {
        logger::Write(L"Run: GetProcAddress failed");
        MessageBoxW(g_hwnd, L"WebView2Loader 版本不兼容", L"错误", MB_ICONERROR);
        return 4;
    }
    
    std::wstring userData = paths::DataDir() + L"WebView2";
    std::wstring webDir = ExeDirOfWeb();
    
        HRESULT hr = createEnv(
        nullptr,
        userData.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                                if (FAILED(result) || !env) return result;
                env->CreateCoreWebView2Controller(g_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&](HRESULT result2, ICoreWebView2Controller* controller) -> HRESULT {
                                                        if (FAILED(result2) || !controller) return result2;
                            g_controller = controller;
                            g_controller->get_CoreWebView2(&g_webview);
                            if (!g_webview) return E_FAIL;

                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [&](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR json = nullptr;
                                        args->get_WebMessageAsJson(&json);
                                        if (json) {
                                            std::string u8 = su::WToUtf8(json);
                                            CoTaskMemFree(json);
                                            HandleMessage(u8);
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

                            ComPtr<ICoreWebView2_3> wv3;
                            g_webview->QueryInterface(IID_PPV_ARGS(&wv3));
                            if (wv3) {
                                wv3->SetVirtualHostNameToFolderMapping(
                                    L"app.mqga", webDir.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            }

                            RECT rc;
                            GetClientRect(g_hwnd, &rc);
                            g_controller->put_Bounds(rc);
                            SetPostFn(&PostToJs);
                            g_webview->Navigate(L"https://app.mqga/index.html");
                            // --selftest：等页面加载后自动发消息验证 交互/日志/后端 全链路
                            if (GetCommandLineW() && wcsstr(GetCommandLineW(), L"--selftest")) {
                                std::thread([]() {
                                    Sleep(3000); // 等页面 JS 注册监听器
                                    HandleMessage("{\"action\":\"locateQq\"}");
                                    Sleep(500);
                                    HandleMessage("{\"action\":\"listVersions\",\"qqRoot\":\"D:\\\\QQNT\"}");
                                    Sleep(500);
                                    HandleMessage("{\"action\":\"install\",\"host\":\"mqga\",\"hostSourceDir\":\"D:\\\\LiteLoaderQQNT\",\"installDir\":\"D:\\\\LiteLoaderQQNT\",\"qqRoot\":\"D:\\\\QQNT\",\"version\":\"9.9.37-54395\"}");
                                    Sleep(500);
                                    HandleMessage("{\"action\":\"checkUpdate\",\"qqRoot\":\"D:\\\\QQNT\"}");
                                    Sleep(500);
                                    HandleMessage("{\"action\":\"listPlugins\",\"installDir\":\"D:/LiteLoaderQQNT\"}");
                                    Sleep(500);
                                    HandleMessage("{\"action\":\"listHosts\"}");
                                }).detach();
                            }
                                                        return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

        if (FAILED(hr)) {
        logger::Write(L"Run: createEnv FAILED");
        MessageBoxW(g_hwnd, L"WebView2 环境创建失败（需要 Edge WebView2 Runtime）", L"错误", MB_ICONERROR);
        return 5;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

} // namespace ui
