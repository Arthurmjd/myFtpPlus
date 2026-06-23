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
    struct LayoutItem {
        HWND hwnd{};
        RECT rect{};
    };

    HWND Place(HWND hwnd, int x, int y, int w, int h);
    void BuildUi();
    void LayoutControls();
    void ResizeListColumns();
    void RefreshStatus();
    void RefreshTransfers();
    void RefreshDirectory(const std::wstring& preferredSelection = L"");
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
    std::string CurrentDirPath() const;
    std::optional<FileEntry> SelectedDirectoryEntry() const;
    void DirectoryUp();
    void DirectoryMake();
    void DirectoryRename();
    void DirectoryDelete();
    void OpenSelectedDirectory();
    void AlertUser(const std::wstring& text) const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_{};
    HWND hwnd_{};
    HFONT uiFont_{};
    HFONT titleFont_{};

    std::vector<LayoutItem> layoutItems_;

    HWND serviceTitle_{};
    HWND serviceLine_{};
    HWND portLabel_{};
    HWND statusLabel_{};

    HWND userTitle_{};
    HWND userLine_{};

    HWND infoTitle_{};
    HWND infoLine_{};
    HWND userNameLabel_{};
    HWND userPassLabel_{};
    HWND homeLabel_{};
    HWND permTitle_{};
    HWND permLine_{};
    std::array<HWND, 4> permHeaderLabels_{};
    std::array<HWND, 4> permAreaLabels_{};

    HWND dirTitle_{};
    HWND dirLine_{};
    HWND dirPathLabel_{};
    HWND dirNameLabel_{};

    HWND transferTitle_{};
    HWND transferLine_{};

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

    HWND dirPath_{};
    HWND dirInput_{};
    HWND dirList_{};
    HWND dirUpBtn_{};
    HWND dirRefreshBtn_{};
    HWND dirMakeBtn_{};
    HWND dirRenameBtn_{};
    HWND dirDeleteBtn_{};
    HWND transferList_{};

    std::array<std::array<HWND, 4>, 4> permissionButtons_{};
    std::vector<FileEntry> dirEntries_;
    ServerCore core_;
};

}  // namespace fds::serverapp
