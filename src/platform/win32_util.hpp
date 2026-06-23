#pragma once

#include "shared/common.hpp"

#include <string>

namespace fds::win32 {

// RAII：进入作用域时初始化 Winsock，离开作用域时自动清理。
class ScopedSockets {
public:
    ScopedSockets();
    ~ScopedSockets();

    ScopedSockets(const ScopedSockets&) = delete;
    ScopedSockets& operator=(const ScopedSockets&) = delete;
};

// RAII：让 COM/OLE 相关对话框可以安全工作。
class ScopedOle {
public:
    ScopedOle();
    ~ScopedOle();

    ScopedOle(const ScopedOle&) = delete;
    ScopedOle& operator=(const ScopedOle&) = delete;
};

// 读取/设置控件文本。
std::wstring GetText(HWND hwnd);
void SetText(HWND hwnd, const std::wstring& value);
// 统一的消息提示框。
void Alert(HWND hwnd, const std::wstring& title, const std::wstring& text);
// 为列表控件添加一列。
void AddColumn(HWND list, int index, int width, const wchar_t* title);
// 常用文件、目录选择对话框封装。
std::wstring PickOpenFile(HWND hwnd);
std::wstring PickSaveFile(HWND hwnd, const std::wstring& initialName);
std::wstring PickFolder(HWND hwnd, const std::wstring& title);

}  // namespace fds::win32
