#pragma once

#include "core.hpp"

#include <array>

namespace fds::serverapp {

class ServerWindow {
public:
    explicit ServerWindow(HINSTANCE instance);
    ~ServerWindow();

    int Run(int showCmd);

private:
    void BuildUi();
    void RefreshStatus();
    void ReloadUsers(const std::wstring& preferredName = L"");
    void ClearUserFields();
    void ApplyUser(const UserRecord& user);
    void FillFromSelection();
    void CreateNewUser();
    void ToggleServer();
    void SaveUser();
    void RemoveUser();
    void UpdatePermissionButtons();
    void UpdateHomeHint();
    void SetPermissionDefaults();
    std::string SuggestedHome(const std::string& username, bool admin) const;
    std::string BuildRuleSpec(const std::string& username, bool admin) const;
    void AlertUser(const std::wstring& text) const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_{};
    HWND hwnd_{};
    HFONT uiFont_{};
    HFONT titleFont_{};

    HWND status_{};
    HWND portEdit_{};
    HWND startBtn_{};
    HWND userList_{};
    HWND userName_{};
    HWND userPass_{};
    HWND homeHint_{};
    HWND userEnabled_{};
    HWND userAdmin_{};
    HWND newBtn_{};
    HWND saveBtn_{};
    HWND deleteBtn_{};
    std::array<std::array<HWND, 4>, 4> permissionButtons_{};
    ServerCore core_;
};

}  // namespace fds::serverapp
