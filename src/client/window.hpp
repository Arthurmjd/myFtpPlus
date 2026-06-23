#pragma once

#include "protocol.hpp"
#include "state.hpp"

namespace fds::clientapp {

// 客户端面板：负责登录、目录浏览、文件操作和任务列表展示。
class ClientWindow {
public:
    explicit ClientWindow(HINSTANCE instance);
    ~ClientWindow();

    int Run(int showCmd);

private:
    // 记录控件基准布局，窗口缩放时按比例重排。
    struct LayoutItem {
        HWND hwnd{};
        RECT rect{};
    };

    // Track 负责把控件归到某个视图分组，Place 负责登记布局信息。
    HWND Track(std::vector<HWND>& bucket, HWND hwnd);
    HWND Place(HWND hwnd, int x, int y, int w, int h);

    // UI 构建与布局。
    void BuildUi();
    void BuildLoginView();
    void BuildClientView();
    void LayoutControls();
    void ResizeListColumns();

    // 登录态切换和状态栏维护。
    void SetView(bool loggedIn);
    void SetStatus(const std::wstring& text);
    void AlertUser(const std::wstring& text) const;
    void UpdateLoginMode();
    void UpdateSessionBanner();
    void ResetBrowserState();

    // 常用连接加载与保存。
    void LoadFavorites();
    void RefreshFavoritesBox();
    void SaveFavorite();
    void LoadFavoriteSelection();

    // 当前目录浏览、文件列表和任务列表刷新。
    bool Browse(const std::string& path, bool pushHistory);
    std::string CurrentPath() const;
    void RefreshFileList();
    void RefreshTasks();

    // 从界面当前选择中取出业务对象。
    std::optional<FileEntry> SelectedEntry() const;
    std::shared_ptr<TransferTask> SelectedTask() const;
    ConnectionInfo CurrentConnection() const;

    // 把上传/下载请求封装成任务并交给 TaskManager。
    void QueueTask(bool upload, const std::wstring& local, const std::string& remote);

    // 主要用户操作入口。
    void ConnectServer();
    void Logout();
    void UploadFile();
    void UploadDirectory();
    void DownloadFile();
    void RemoveEntry();
    void MakeDir();
    void RenameEntry();
    void PauseTask();
    void ResumeTask();
    void CancelTask();
    void Back();
    void Forward();
    void Up();
    void EnterSelected();

    // Win32 消息分发辅助函数。
    LRESULT HandleCommand(WPARAM wParam, LPARAM lParam);
    LRESULT HandleNotify(LPARAM lParam);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_{};
    HWND hwnd_{};
    HFONT uiFont_{};
    HFONT titleFont_{};

    HWND hostEdit_{};
    HWND portEdit_{};
    HWND userLabel_{};
    HWND userEdit_{};
    HWND passLabel_{};
    HWND passEdit_{};
    HWND modeUserRadio_{};
    HWND modeAnonRadio_{};
    HWND favoritesBox_{};

    HWND currentUser_{};
    HWND pathEdit_{};
    HWND fileList_{};
    HWND inputEdit_{};
    HWND taskList_{};
    HWND status_{};

    std::vector<HWND> loginControls_;
    std::vector<HWND> clientControls_;
    std::vector<LayoutItem> layoutItems_;

    FavoriteStore favorites_;
    CommandClient client_;
    TaskManager taskManager_;
    NavigationHistory history_;
    std::vector<FileEntry> entries_;
    bool loggedIn_ = false;
};

}  // namespace fds::clientapp
