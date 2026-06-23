#pragma once

#include "protocol.hpp"
#include "state.hpp"

namespace fds::clientapp {

class ClientWindow {
public:
    explicit ClientWindow(HINSTANCE instance);
    ~ClientWindow();

    int Run(int showCmd);

private:
    struct LayoutItem {
        HWND hwnd{};
        RECT rect{};
    };

    HWND Track(std::vector<HWND>& bucket, HWND hwnd);
    HWND Place(HWND hwnd, int x, int y, int w, int h);

    void BuildUi();
    void BuildLoginView();
    void BuildClientView();
    void LayoutControls();
    void ResizeListColumns();

    void SetView(bool loggedIn);
    void SetStatus(const std::wstring& text);
    void AlertUser(const std::wstring& text) const;
    void UpdateLoginMode();
    void UpdateSessionBanner();
    void ResetBrowserState();

    void LoadFavorites();
    void RefreshFavoritesBox();
    void SaveFavorite();
    void LoadFavoriteSelection();

    bool Browse(const std::string& path, bool pushHistory);
    std::string CurrentPath() const;
    void RefreshFileList();
    void RefreshTasks();

    std::optional<FileEntry> SelectedEntry() const;
    std::shared_ptr<TransferTask> SelectedTask() const;
    ConnectionInfo CurrentConnection() const;

    void QueueTask(bool upload, const std::wstring& local, const std::string& remote);

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
