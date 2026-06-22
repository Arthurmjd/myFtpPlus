#pragma once

#include "shared/common.hpp"

#include <string>

namespace fds::win32 {

class ScopedSockets {
public:
    ScopedSockets();
    ~ScopedSockets();

    ScopedSockets(const ScopedSockets&) = delete;
    ScopedSockets& operator=(const ScopedSockets&) = delete;
};

class ScopedOle {
public:
    ScopedOle();
    ~ScopedOle();

    ScopedOle(const ScopedOle&) = delete;
    ScopedOle& operator=(const ScopedOle&) = delete;
};

std::wstring GetText(HWND hwnd);
void SetText(HWND hwnd, const std::wstring& value);
void Alert(HWND hwnd, const std::wstring& title, const std::wstring& text);
void AddColumn(HWND list, int index, int width, const wchar_t* title);
std::wstring PickOpenFile(HWND hwnd);
std::wstring PickSaveFile(HWND hwnd, const std::wstring& initialName);
std::wstring PickFolder(HWND hwnd, const std::wstring& title);

}  // namespace fds::win32
