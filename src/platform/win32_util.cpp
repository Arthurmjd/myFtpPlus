#include "win32_util.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

namespace fds::win32 {

ScopedSockets::ScopedSockets() {
    InitSockets();
}

ScopedSockets::~ScopedSockets() {
    CleanupSockets();
}

ScopedOle::ScopedOle() {
    OleInitialize(nullptr);
}

ScopedOle::~ScopedOle() {
    OleUninitialize();
}

std::wstring GetText(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) {
        return L"";
    }
    std::wstring value(static_cast<std::size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, value.data(), len + 1);
    value.resize(static_cast<std::size_t>(len));
    return value;
}

void SetText(HWND hwnd, const std::wstring& value) {
    SetWindowTextW(hwnd, value.c_str());
}

void Alert(HWND hwnd, const std::wstring& title, const std::wstring& text) {
    MessageBoxW(hwnd, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

void AddColumn(HWND list, int index, int width, const wchar_t* title) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.cx = width;
    column.pszText = const_cast<wchar_t*>(title);
    column.iSubItem = index;
    ListView_InsertColumn(list, index, &column);
}

std::wstring PickOpenFile(HWND hwnd) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"All Files\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&dialog) ? std::wstring(file) : L"";
}

std::wstring PickSaveFile(HWND hwnd, const std::wstring& initialName) {
    wchar_t file[MAX_PATH]{};
    wcsncpy_s(file, initialName.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"All Files\0*.*\0";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return GetSaveFileNameW(&dialog) ? std::wstring(file) : L"";
}

std::wstring PickFolder(HWND hwnd, const std::wstring& title) {
    BROWSEINFOW dialog{};
    dialog.hwndOwner = hwnd;
    dialog.lpszTitle = title.c_str();
    dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&dialog);
    if (!pidl) {
        return L"";
    }

    wchar_t path[MAX_PATH]{};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return path;
}

}  // namespace fds::win32
