#pragma once

#include "core.hpp"

namespace fds::serverapp {

class ServerWindow {
public:
    explicit ServerWindow(HINSTANCE instance);

    int Run(int showCmd);

private:
    void BuildUi();
    void RefreshUi();
    void ClearUserFields();
    void ApplyUser(const UserRecord& user);
    void FillFromSelection();
    void ToggleServer();
    void SaveUser();
    void RemoveUser();
    void AlertUser(const std::wstring& text) const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND status_{};
    HWND portEdit_{};
    HWND startBtn_{};
    HWND userList_{};
    HWND userName_{};
    HWND userPass_{};
    HWND userHome_{};
    HWND userRules_{};
    HWND userEnabled_{};
    HWND userAdmin_{};
    HWND saveBtn_{};
    HWND deleteBtn_{};
    HWND logs_{};
    ServerCore core_;
};

}  // namespace fds::serverapp
