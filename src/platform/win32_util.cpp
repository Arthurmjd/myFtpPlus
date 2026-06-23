#include "win32_util.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

namespace fds::win32 {

// 在 GUI 程序启动时自动准备 socket 环境。
ScopedSockets::ScopedSockets() {
    InitSockets();
}

// 程序退出时释放 socket 环境。
ScopedSockets::~ScopedSockets() {
    CleanupSockets();
}

// 初始化 OLE，便于后续打开系统文件/目录选择器。
ScopedOle::ScopedOle() {
    OleInitialize(nullptr);
}

// 对称释放 OLE 资源。
ScopedOle::~ScopedOle() {
    OleUninitialize();
}

// 读取一个 Win32 控件中的文本内容。
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

// 设置控件文本。
void SetText(HWND hwnd, const std::wstring& value) {
    SetWindowTextW(hwnd, value.c_str());
}

// 弹出统一风格的信息提示框。
void Alert(HWND hwnd, const std::wstring& title, const std::wstring& text) {
    MessageBoxW(hwnd, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

// 给列表控件插入一列，减少重复样板代码。
void AddColumn(HWND list, int index, int width, const wchar_t* title) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.cx = width;
    column.pszText = const_cast<wchar_t*>(title);
    column.iSubItem = index;
    ListView_InsertColumn(list, index, &column);
}

// 打开“选择已有文件”对话框。
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

// 打开“另存为”对话框，并预填建议文件名。
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

// 打开“选择文件夹”对话框。
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
