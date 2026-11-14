// pickfolder.cpp — 目录选择对话框。完全自包含结构体声明 + 动态加载 shell32。
// 离线 SDK 的 shell 头文件链断裂（ShlObj_core.h → isguids.h 缺失），故手写声明。
#include "pickfolder.h"
#include <windows.h>
#include <string>

// ---- 自包含 shell 结构体（与官方 ABI 兼容） ----
typedef struct _ITEMIDLIST { struct { unsigned short cb; unsigned char abID[1]; } mkid; } ITEMIDLIST;
typedef ITEMIDLIST* LPITEMIDLIST;
typedef const ITEMIDLIST* PCIDLIST_ABSOLUTE;

typedef struct _browseinfoW {
    HWND hwndOwner;
    PCIDLIST_ABSOLUTE pidlRoot;
    LPWSTR pszDisplayName;
    LPCWSTR lpszTitle;
    UINT ulFlags;
    void (*lpfn)(HWND, UINT, LPARAM, LPARAM);
    LPARAM lParam;
    int iImage;
} BROWSEINFOW;

#ifndef BIF_RETURNONLYFSDIRS
#define BIF_RETURNONLYFSDIRS 0x00000001
#endif
#ifndef BIF_NEWDIALOGSTYLE
#define BIF_NEWDIALOGSTYLE 0x00000040
#endif
#ifndef BIF_NONEWFOLDERBUTTON
#define BIF_NONEWFOLDERBUTTON 0x00000200
#endif

namespace ui {

std::wstring PickFolder(const std::wstring& title, const std::wstring& initial)
{
    typedef LPITEMIDLIST(WINAPI* SHBrowseForFolderWFn)(BROWSEINFOW*);
    typedef BOOL(WINAPI* SHGetPathFromIDListWFn)(PCIDLIST_ABSOLUTE, LPWSTR);

    HMODULE shell = LoadLibraryW(L"shell32.dll");
    if (!shell) return L"";
    auto browseForFolder = reinterpret_cast<SHBrowseForFolderWFn>(
        GetProcAddress(shell, "SHBrowseForFolderW"));
    auto pathFromIdList = reinterpret_cast<SHGetPathFromIDListWFn>(
        GetProcAddress(shell, "SHGetPathFromIDListW"));
    if (!browseForFolder || !pathFromIdList) {
        FreeLibrary(shell);
        return L"";
    }

    wchar_t displayBuffer[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = GetActiveWindow();
    bi.pidlRoot = nullptr;
    bi.pszDisplayName = displayBuffer;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    bi.lpfn = nullptr;
    bi.lParam = reinterpret_cast<LPARAM>(initial.c_str());

    std::wstring result;
    LPITEMIDLIST pidl = browseForFolder(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH]{};
        if (pathFromIdList(pidl, path)) result = path;
        // 用 IMalloc 释放 PIDL（标准做法，CoTaskMemFree 需 ole32 头）
        IMalloc* malloc_ = nullptr;
        if (SUCCEEDED(CoGetMalloc(1, &malloc_)) && malloc_) {
            malloc_->Free(pidl);
            malloc_->Release();
        }
    }
    FreeLibrary(shell);
    return result;
}

} // namespace ui
