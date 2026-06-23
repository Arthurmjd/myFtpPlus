#pragma once

#include "core.hpp"

#include <array>

namespace fds::serverapp {

// 服务端管理员面板：负责服务开关、用户管理、目录管理和传输列表展示。
class ServerWindow {
public:
    explicit ServerWindow(HINSTANCE instance);
    ~ServerWindow();

    int Run(int showCmd);

private:
    // 记录基准布局，窗口缩放时按比例重排控件。
    struct LayoutItem {
        HWND hwnd{};
        RECT rect{};
    };

    // Place 会登记控件原始位置，供后续统一布局使用。
    HWND Place(HWND hwnd, int x, int y, int w, int h);
    // UI 构建与刷新。
    void BuildUi();
    void LayoutControls();
    void ResizeListColumns();
    void RefreshStatus();
    void RefreshTransfers();
    void RefreshDirectory(const std::wstring& preferredSelection = L"");
    void ReloadUsers(const std::wstring& preferredName = L"");
    // 用户表单与权限按钮联动。
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
    // 管理员目录管理相关操作。
    std::string CurrentDirPath() const;
    std::optional<FileEntry> SelectedDirectoryEntry() const;
    void DirectoryUp();
    void DirectoryMake();
    void DirectoryRename();
    void DirectoryDelete();
    void OpenSelectedDirectory();
    // 统一弹出提示框。
    void AlertUser(const std::wstring& text) const;

    // Win32 窗口消息入口。
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
