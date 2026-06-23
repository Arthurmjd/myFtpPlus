#include "window.hpp"

#include "platform/win32_util.hpp"

#include <commctrl.h>
#include <windowsx.h>

namespace fds::clientapp {

namespace {

constexpr int IDC_HOST = 2001;
constexpr int IDC_PORT = 2002;
constexpr int IDC_USER = 2003;
constexpr int IDC_PASS = 2004;
constexpr int IDC_MODE_USER = 2005;
constexpr int IDC_MODE_ANON = 2006;
constexpr int IDC_CONNECT = 2007;
constexpr int IDC_SAVE_FAV = 2008;
constexpr int IDC_FAVORITES = 2009;
constexpr int IDC_LOAD_FAV = 2010;
constexpr int IDC_LOGOUT = 2011;
constexpr int IDC_PATH = 2012;
constexpr int IDC_BACK = 2013;
constexpr int IDC_FORWARD = 2014;
constexpr int IDC_UP = 2015;
constexpr int IDC_REFRESH = 2016;
constexpr int IDC_FILES = 2017;
constexpr int IDC_UPLOAD_FILE = 2018;
constexpr int IDC_UPLOAD_DIR = 2019;
constexpr int IDC_DOWNLOAD = 2020;
constexpr int IDC_DELETE = 2021;
constexpr int IDC_MKDIR = 2022;
constexpr int IDC_RENAME = 2023;
constexpr int IDC_INPUT = 2024;
constexpr int IDC_TASKS = 2025;
constexpr int IDC_PAUSE = 2026;
constexpr int IDC_RESUME = 2027;
constexpr int IDC_CANCEL = 2028;
constexpr int IDC_STATUS = 2029;

constexpr UINT_PTR kRefreshTimerId = 1;

}  // namespace

ClientWindow::ClientWindow(HINSTANCE instance)
    : instance_(instance),
      favorites_(std::filesystem::current_path() / L"data" / L"client_favorites.tsv") {}

int ClientWindow::Run(int showCmd) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &ClientWindow::WndProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = L"FdsClientWindow";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&windowClass);

    hwnd_ = CreateWindowW(windowClass.lpszClassName, L"FDS 客户端", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          1180, 760, nullptr, nullptr, instance_, this);
    ShowWindow(hwnd_, showCmd);
    UpdateWindow(hwnd_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

HWND ClientWindow::Track(std::vector<HWND>& bucket, HWND hwnd) {
    if (hwnd) {
        bucket.push_back(hwnd);
    }
    return hwnd;
}

void ClientWindow::BuildUi() {
    BuildLoginView();
    BuildClientView();

    status_ = CreateWindowW(L"STATIC", L"请先连接服务器", WS_CHILD | WS_VISIBLE, 16, 680, 740, 24, hwnd_,
                            reinterpret_cast<HMENU>(IDC_STATUS), instance_, nullptr);

    LoadFavorites();
    UpdateLoginMode();
    SetView(false);
    SetTimer(hwnd_, kRefreshTimerId, 500, nullptr);
}

void ClientWindow::BuildLoginView() {
    Track(loginControls_, CreateWindowW(L"STATIC", L"远程文件客户端下载器", WS_CHILD | WS_VISIBLE | SS_CENTER, 350, 60, 420,
                                        34, hwnd_, nullptr, instance_, nullptr));
    Track(loginControls_, CreateWindowW(L"STATIC", L"请选择用户登录或匿名登录", WS_CHILD | WS_VISIBLE | SS_CENTER, 340, 96,
                                        440, 24, hwnd_, nullptr, instance_, nullptr));
    Track(loginControls_, CreateWindowW(L"BUTTON", L"连接设置", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 250, 138, 660, 250,
                                        hwnd_, nullptr, instance_, nullptr));

    Track(loginControls_, CreateWindowW(L"STATIC", L"服务器", WS_CHILD | WS_VISIBLE, 300, 182, 70, 24, hwnd_, nullptr,
                                        instance_, nullptr));
    hostEdit_ = Track(loginControls_, CreateWindowW(L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER, 370, 178,
                                                    170, 26, hwnd_, reinterpret_cast<HMENU>(IDC_HOST), instance_,
                                                    nullptr));
    Track(loginControls_, CreateWindowW(L"STATIC", L"端口", WS_CHILD | WS_VISIBLE, 570, 182, 50, 24, hwnd_, nullptr,
                                        instance_, nullptr));
    portEdit_ = Track(loginControls_, CreateWindowW(L"EDIT", L"9527", WS_CHILD | WS_VISIBLE | WS_BORDER, 620, 178, 100,
                                                    26, hwnd_, reinterpret_cast<HMENU>(IDC_PORT), instance_, nullptr));

    modeUserRadio_ = Track(loginControls_,
                           CreateWindowW(L"BUTTON", L"用户登录",
                                         WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON, 370, 222, 110, 24,
                                         hwnd_, reinterpret_cast<HMENU>(IDC_MODE_USER), instance_, nullptr));
    modeAnonRadio_ = Track(loginControls_,
                           CreateWindowW(L"BUTTON", L"匿名登录", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 500, 222,
                                         110, 24, hwnd_, reinterpret_cast<HMENU>(IDC_MODE_ANON), instance_, nullptr));
    Button_SetCheck(modeUserRadio_, BST_CHECKED);

    userLabel_ = Track(loginControls_, CreateWindowW(L"STATIC", L"用户名", WS_CHILD | WS_VISIBLE, 300, 266, 70, 24,
                                                     hwnd_, nullptr, instance_, nullptr));
    userEdit_ = Track(loginControls_, CreateWindowW(L"EDIT", L"demo", WS_CHILD | WS_VISIBLE | WS_BORDER, 370, 262, 170,
                                                    26, hwnd_, reinterpret_cast<HMENU>(IDC_USER), instance_, nullptr));
    passLabel_ = Track(loginControls_, CreateWindowW(L"STATIC", L"密码", WS_CHILD | WS_VISIBLE, 570, 266, 50, 24, hwnd_,
                                                     nullptr, instance_, nullptr));
    passEdit_ =
        Track(loginControls_, CreateWindowW(L"EDIT", L"demo123", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD, 620,
                                            262, 160, 26, hwnd_, reinterpret_cast<HMENU>(IDC_PASS), instance_, nullptr));

    Track(loginControls_, CreateWindowW(L"STATIC", L"常用连接", WS_CHILD | WS_VISIBLE, 300, 306, 70, 24, hwnd_, nullptr,
                                        instance_, nullptr));
    favoritesBox_ = Track(loginControls_,
                          CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST, 370, 302,
                                        320, 220, hwnd_, reinterpret_cast<HMENU>(IDC_FAVORITES), instance_, nullptr));
    Track(loginControls_, CreateWindowW(L"BUTTON", L"载入", WS_CHILD | WS_VISIBLE, 705, 300, 75, 28, hwnd_,
                                        reinterpret_cast<HMENU>(IDC_LOAD_FAV), instance_, nullptr));
    Track(loginControls_, CreateWindowW(L"BUTTON", L"保存连接", WS_CHILD | WS_VISIBLE, 370, 340, 120, 30, hwnd_,
                                        reinterpret_cast<HMENU>(IDC_SAVE_FAV), instance_, nullptr));
    Track(loginControls_, CreateWindowW(L"BUTTON", L"进入客户端", WS_CHILD | WS_VISIBLE, 660, 340, 120, 30, hwnd_,
                                        reinterpret_cast<HMENU>(IDC_CONNECT), instance_, nullptr));
    Track(loginControls_,
          CreateWindowW(L"STATIC", L"匿名登录仅显示公共目录和下载目录；用户登录会显示用户目录、公共目录、下载目录、上传目录。",
                        WS_CHILD | WS_VISIBLE, 285, 405, 600, 24, hwnd_, nullptr, instance_, nullptr));
}

void ClientWindow::BuildClientView() {
    currentUser_ = Track(clientControls_,
                         CreateWindowW(L"STATIC", L"当前登录：未登录", WS_CHILD | WS_VISIBLE, 16, 20, 780, 24, hwnd_,
                                       nullptr, instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"退出登录", WS_CHILD | WS_VISIBLE, 1010, 14, 120, 30, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_LOGOUT), instance_, nullptr));

    Track(clientControls_, CreateWindowW(L"BUTTON", L"<", WS_CHILD | WS_VISIBLE, 16, 56, 32, 28, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_BACK), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L">", WS_CHILD | WS_VISIBLE, 52, 56, 32, 28, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_FORWARD), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"上级", WS_CHILD | WS_VISIBLE, 88, 56, 60, 28, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_UP), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE, 154, 56, 60, 28, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_REFRESH), instance_, nullptr));
    pathEdit_ = Track(clientControls_,
                      CreateWindowW(L"EDIT", L"/", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY, 224, 58, 500, 24,
                                    hwnd_, reinterpret_cast<HMENU>(IDC_PATH), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"STATIC", L"名称", WS_CHILD | WS_VISIBLE, 742, 60, 50, 24, hwnd_, nullptr,
                                         instance_, nullptr));
    inputEdit_ = Track(clientControls_,
                       CreateWindowW(L"EDIT", L"新目录", WS_CHILD | WS_VISIBLE | WS_BORDER, 792, 58, 230, 24, hwnd_,
                                     reinterpret_cast<HMENU>(IDC_INPUT), instance_, nullptr));

    fileList_ = Track(clientControls_,
                      CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                    16, 96, 740, 360, hwnd_, reinterpret_cast<HMENU>(IDC_FILES), instance_, nullptr));
    ListView_SetExtendedListViewStyle(fileList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    win32::AddColumn(fileList_, 0, 220, L"名称");
    win32::AddColumn(fileList_, 1, 80, L"类型");
    win32::AddColumn(fileList_, 2, 120, L"大小");
    win32::AddColumn(fileList_, 3, 180, L"修改时间");
    win32::AddColumn(fileList_, 4, 220, L"路径");

    Track(clientControls_, CreateWindowW(L"BUTTON", L"上传文件", WS_CHILD | WS_VISIBLE, 780, 110, 110, 30, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_UPLOAD_FILE), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"上传目录", WS_CHILD | WS_VISIBLE, 900, 110, 110, 30, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_UPLOAD_DIR), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"下载文件", WS_CHILD | WS_VISIBLE, 1020, 110, 110, 30, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_DOWNLOAD), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE, 780, 150, 110, 30, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_DELETE), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"新建目录", WS_CHILD | WS_VISIBLE, 900, 150, 110, 30, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_MKDIR), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"重命名", WS_CHILD | WS_VISIBLE, 1020, 150, 110, 30, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_RENAME), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"STATIC", L"登录后默认先显示根目录，双击即可进入对应目录入口。", WS_CHILD | WS_VISIBLE,
                                         780, 200, 320, 24, hwnd_, nullptr, instance_, nullptr));
    Track(clientControls_,
          CreateWindowW(L"STATIC", L"建议先进入“用户目录”或“上传目录”再执行上传、创建目录等写入操作。", WS_CHILD | WS_VISIBLE,
                        780, 226, 340, 36, hwnd_, nullptr, instance_, nullptr));

    taskList_ = Track(clientControls_,
                      CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                    16, 474, 1114, 190, hwnd_, reinterpret_cast<HMENU>(IDC_TASKS), instance_, nullptr));
    ListView_SetExtendedListViewStyle(taskList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    win32::AddColumn(taskList_, 0, 50, L"ID");
    win32::AddColumn(taskList_, 1, 70, L"类型");
    win32::AddColumn(taskList_, 2, 280, L"本地路径");
    win32::AddColumn(taskList_, 3, 280, L"远程路径");
    win32::AddColumn(taskList_, 4, 150, L"进度");
    win32::AddColumn(taskList_, 5, 100, L"状态");
    win32::AddColumn(taskList_, 6, 170, L"说明");

    Track(clientControls_, CreateWindowW(L"BUTTON", L"暂停", WS_CHILD | WS_VISIBLE, 780, 676, 90, 28, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_PAUSE), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"继续", WS_CHILD | WS_VISIBLE, 878, 676, 90, 28, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_RESUME), instance_, nullptr));
    Track(clientControls_, CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE, 976, 676, 90, 28, hwnd_,
                                         reinterpret_cast<HMENU>(IDC_CANCEL), instance_, nullptr));
}

void ClientWindow::SetView(bool loggedIn) {
    loggedIn_ = loggedIn;
    for (HWND control : loginControls_) {
        ShowWindow(control, loggedIn ? SW_HIDE : SW_SHOW);
        EnableWindow(control, !loggedIn);
    }
    for (HWND control : clientControls_) {
        ShowWindow(control, loggedIn ? SW_SHOW : SW_HIDE);
        EnableWindow(control, loggedIn);
    }
    SetFocus(loggedIn ? fileList_ : hostEdit_);
}

void ClientWindow::SetStatus(const std::wstring& text) {
    win32::SetText(status_, text);
}

void ClientWindow::AlertUser(const std::wstring& text) const {
    win32::Alert(hwnd_, L"FDS 客户端", text);
}

void ClientWindow::UpdateLoginMode() {
    const bool anonymous = Button_GetCheck(modeAnonRadio_) == BST_CHECKED;
    EnableWindow(userLabel_, !anonymous);
    EnableWindow(userEdit_, !anonymous);
    EnableWindow(passLabel_, !anonymous);
    EnableWindow(passEdit_, !anonymous);
}

void ClientWindow::UpdateSessionBanner() {
    std::wstring role = L"用户";
    if (client_.Username() == "anonymous") {
        role = L"匿名";
    } else if (client_.IsAdmin()) {
        role = L"管理员";
    }

    std::wstring text = L"当前登录：" + Utf8ToWide(client_.Username()) + L"（" + role + L"）";
    if (!client_.Home().empty() && client_.Username() != "anonymous") {
        text += L"    主目录：" + Utf8ToWide(client_.Home());
    }
    win32::SetText(currentUser_, text);
}

void ClientWindow::ResetBrowserState() {
    entries_.clear();
    history_.Reset();
    win32::SetText(pathEdit_, L"/");
    RefreshFileList();
}

void ClientWindow::LoadFavorites() {
    favorites_.Load();
    RefreshFavoritesBox();
}

void ClientWindow::RefreshFavoritesBox() {
    const int selected = static_cast<int>(SendMessageW(favoritesBox_, CB_GETCURSEL, 0, 0));
    SendMessageW(favoritesBox_, CB_RESETCONTENT, 0, 0);

    const auto labels = favorites_.Labels();
    for (const auto& label : labels) {
        SendMessageW(favoritesBox_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }

    if (!labels.empty()) {
        const int newSelection = selected >= 0 && selected < static_cast<int>(labels.size()) ? selected : 0;
        SendMessageW(favoritesBox_, CB_SETCURSEL, newSelection, 0);
    }
}

void ClientWindow::SaveFavorite() {
    Favorite favorite;
    favorite.host = WideToUtf8(win32::GetText(hostEdit_));
    favorite.port = _wtoi(win32::GetText(portEdit_).c_str());
    favorite.user = WideToUtf8(win32::GetText(userEdit_));
    favorite.anonymous = Button_GetCheck(modeAnonRadio_) == BST_CHECKED;

    if (favorite.host.empty()) {
        AlertUser(L"请先填写服务器地址");
        return;
    }
    if (favorite.port <= 0) {
        favorite.port = kDefaultPort;
    }

    if (!favorites_.Add(favorite)) {
        SetStatus(L"连接配置已存在");
        return;
    }

    RefreshFavoritesBox();
    SetStatus(L"连接配置已保存");
}

void ClientWindow::LoadFavoriteSelection() {
    const int index = static_cast<int>(SendMessageW(favoritesBox_, CB_GETCURSEL, 0, 0));
    const Favorite* favorite = favorites_.Get(static_cast<std::size_t>(index));
    if (!favorite) {
        return;
    }

    win32::SetText(hostEdit_, Utf8ToWide(favorite->host));
    win32::SetText(portEdit_, Utf8ToWide(std::to_string(favorite->port)));
    win32::SetText(userEdit_, Utf8ToWide(favorite->user));
    Button_SetCheck(modeUserRadio_, favorite->anonymous ? BST_UNCHECKED : BST_CHECKED);
    Button_SetCheck(modeAnonRadio_, favorite->anonymous ? BST_CHECKED : BST_UNCHECKED);
    UpdateLoginMode();
}

bool ClientWindow::Browse(const std::string& path, bool pushHistory) {
    if (!client_.Connected()) {
        return false;
    }

    std::string error;
    std::string cwd;
    std::vector<FileEntry> items;
    if (!client_.List(path, cwd, items, error)) {
        AlertUser(Utf8ToWide(error));
        return false;
    }

    entries_ = std::move(items);
    win32::SetText(pathEdit_, Utf8ToWide(cwd));
    if (pushHistory) {
        history_.Push(cwd);
    }
    RefreshFileList();
    SetStatus(L"当前目录：" + Utf8ToWide(cwd));
    return true;
}

std::string ClientWindow::CurrentPath() const {
    return WideToUtf8(win32::GetText(pathEdit_));
}

void ClientWindow::RefreshFileList() {
    ListView_DeleteAllItems(fileList_);
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& entry = entries_[i];
        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = i;

        auto name = Utf8ToWide(entry.name);
        row.pszText = name.data();
        ListView_InsertItem(fileList_, &row);

        auto type = std::wstring(entry.isDir ? L"目录" : L"文件");
        auto size = Utf8ToWide(entry.isDir ? "-" : FormatBytes(entry.size));
        auto time = Utf8ToWide(entry.mtime);
        auto path = Utf8ToWide(entry.path);

        ListView_SetItemText(fileList_, i, 1, type.data());
        ListView_SetItemText(fileList_, i, 2, size.data());
        ListView_SetItemText(fileList_, i, 3, time.data());
        ListView_SetItemText(fileList_, i, 4, path.data());
    }
}

void ClientWindow::RefreshTasks() {
    const auto& items = taskManager_.Items();
    const int selectedIndex = ListView_GetNextItem(taskList_, -1, LVNI_SELECTED);
    int selectedTaskId = -1;
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
        selectedTaskId = items[selectedIndex]->id;
    }

    ListView_DeleteAllItems(taskList_);
    int selectedRow = -1;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const auto& task = items[i];
        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = i;

        auto id = Utf8ToWide(std::to_string(task->id));
        row.pszText = id.data();
        ListView_InsertItem(taskList_, &row);

        auto type = std::wstring(task->upload ? L"上传" : L"下载");
        auto local = task->local;
        auto remote = Utf8ToWide(task->remote);
        auto progress = Utf8ToWide(FormatBytes(task->done.load())) + L" / " + Utf8ToWide(FormatBytes(task->total.load()));
        auto state = TaskStateText(task->state.load());
        std::wstring info;
        {
            std::lock_guard lock(task->infoMu);
            info = task->info;
        }

        ListView_SetItemText(taskList_, i, 1, type.data());
        ListView_SetItemText(taskList_, i, 2, local.data());
        ListView_SetItemText(taskList_, i, 3, remote.data());
        ListView_SetItemText(taskList_, i, 4, progress.data());
        ListView_SetItemText(taskList_, i, 5, state.data());
        ListView_SetItemText(taskList_, i, 6, info.data());

        if (task->id == selectedTaskId) {
            selectedRow = i;
        }
    }

    if (selectedRow >= 0) {
        ListView_SetItemState(taskList_, selectedRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

std::optional<FileEntry> ClientWindow::SelectedEntry() const {
    const int index = ListView_GetNextItem(fileList_, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return std::nullopt;
    }
    return entries_[index];
}

std::shared_ptr<TransferTask> ClientWindow::SelectedTask() const {
    const int index = ListView_GetNextItem(taskList_, -1, LVNI_SELECTED);
    return taskManager_.GetAt(index);
}

ConnectionInfo ClientWindow::CurrentConnection() const {
    return {client_.Host(), client_.Port(), client_.Session()};
}

void ClientWindow::QueueTask(bool upload, const std::wstring& local, const std::string& remote) {
    taskManager_.Add(upload, local, remote, CurrentConnection());
    RefreshTasks();
    SetStatus(upload ? L"上传任务已加入队列" : L"下载任务已加入队列");
}

void ClientWindow::ConnectServer() {
    if (loggedIn_) {
        return;
    }

    const std::string host = WideToUtf8(win32::GetText(hostEdit_));
    int port = _wtoi(win32::GetText(portEdit_).c_str());
    if (host.empty()) {
        AlertUser(L"请填写服务器地址");
        return;
    }
    if (port <= 0) {
        port = kDefaultPort;
    }

    const bool anonymous = Button_GetCheck(modeAnonRadio_) == BST_CHECKED;
    const std::string user = anonymous ? "" : WideToUtf8(win32::GetText(userEdit_));
    const std::string pass = anonymous ? "" : WideToUtf8(win32::GetText(passEdit_));
    if (!anonymous && user.empty()) {
        AlertUser(L"用户登录必须填写用户名");
        return;
    }

    std::string error;
    if (!client_.Connect(host, port, user, pass, anonymous, error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    ResetBrowserState();
    if (!Browse("/", true)) {
        client_.Disconnect();
        return;
    }

    UpdateSessionBanner();
    SetView(true);
    SetStatus(L"连接成功，已进入客户端界面");
}

void ClientWindow::Logout() {
    client_.Disconnect();
    ResetBrowserState();
    win32::SetText(currentUser_, L"当前登录：未登录");
    SetView(false);
    SetStatus(L"已退出登录");
}

void ClientWindow::UploadFile() {
    if (!client_.Connected()) {
        return;
    }

    const auto file = win32::PickOpenFile(hwnd_);
    if (file.empty()) {
        return;
    }

    const auto name = WideToUtf8(std::filesystem::path(file).filename().wstring());
    QueueTask(true, file, JoinRemotePath(CurrentPath(), name));
}

void ClientWindow::UploadDirectory() {
    if (!client_.Connected()) {
        return;
    }

    const auto directory = win32::PickFolder(hwnd_, L"选择要上传的目录");
    if (directory.empty()) {
        return;
    }

    const auto baseName = WideToUtf8(std::filesystem::path(directory).filename().wstring());
    const auto remoteBase = JoinRemotePath(CurrentPath(), baseName);

    std::string error;
    if (!client_.MakeDir(remoteBase, error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(directory, ec);
    const std::filesystem::recursive_directory_iterator end;
    while (!ec && it != end) {
        const auto entry = *it;
        const auto relative = entry.path().lexically_relative(directory);
        const auto remote = JoinRemotePath(remoteBase, WideToUtf8(relative.generic_wstring()));
        if (entry.is_directory()) {
            if (!client_.MakeDir(remote, error)) {
                AlertUser(Utf8ToWide(error));
                return;
            }
        } else if (entry.is_regular_file()) {
            QueueTask(true, entry.path().wstring(), remote);
        }
        it.increment(ec);
    }

    if (ec) {
        AlertUser(L"遍历本地目录失败");
        return;
    }

    Browse(CurrentPath(), false);
    SetStatus(L"目录上传任务已加入队列");
}

void ClientWindow::DownloadFile() {
    if (!client_.Connected()) {
        return;
    }

    const auto selected = SelectedEntry();
    if (!selected || selected->isDir) {
        AlertUser(L"请选择一个文件再下载");
        return;
    }

    const auto local = win32::PickSaveFile(hwnd_, Utf8ToWide(selected->name));
    if (local.empty()) {
        return;
    }

    QueueTask(false, local, selected->path);
}

void ClientWindow::RemoveEntry() {
    if (!client_.Connected()) {
        return;
    }

    const auto selected = SelectedEntry();
    if (!selected) {
        return;
    }

    std::string error;
    if (!client_.Remove(selected->path, error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    Browse(CurrentPath(), false);
    SetStatus(L"删除成功");
}

void ClientWindow::MakeDir() {
    if (!client_.Connected()) {
        return;
    }

    const auto name = Trim(WideToUtf8(win32::GetText(inputEdit_)));
    if (name.empty()) {
        AlertUser(L"请输入目录名称");
        return;
    }

    std::string error;
    if (!client_.MakeDir(JoinRemotePath(CurrentPath(), name), error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    Browse(CurrentPath(), false);
    SetStatus(L"目录创建成功");
}

void ClientWindow::RenameEntry() {
    if (!client_.Connected()) {
        return;
    }

    const auto selected = SelectedEntry();
    if (!selected) {
        return;
    }

    const auto newName = Trim(WideToUtf8(win32::GetText(inputEdit_)));
    if (newName.empty()) {
        AlertUser(L"请输入新的名称");
        return;
    }

    std::string error;
    if (!client_.Rename(selected->path, newName, error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    Browse(CurrentPath(), false);
    SetStatus(L"重命名成功");
}

void ClientWindow::PauseTask() {
    if (const auto task = SelectedTask()) {
        taskManager_.Pause(task);
        SetStatus(L"已请求暂停任务");
    }
}

void ClientWindow::ResumeTask() {
    if (!client_.Connected()) {
        AlertUser(L"请先连接服务器");
        return;
    }
    if (const auto task = SelectedTask()) {
        taskManager_.Resume(task, CurrentConnection());
        SetStatus(L"已请求继续任务");
    }
}

void ClientWindow::CancelTask() {
    if (const auto task = SelectedTask()) {
        taskManager_.Cancel(task);
        SetStatus(L"已请求取消任务");
    }
}

void ClientWindow::Back() {
    if (const auto path = history_.Back()) {
        Browse(*path, false);
    }
}

void ClientWindow::Forward() {
    if (const auto path = history_.Forward()) {
        Browse(*path, false);
    }
}

void ClientWindow::Up() {
    const auto current = NormalizeVirtualPath(CurrentPath());
    if (current.empty() || current == "/") {
        return;
    }

    const auto pos = current.find_last_of('/');
    const auto parent = pos <= 0 ? "/" : current.substr(0, pos);
    Browse(parent, true);
}

void ClientWindow::EnterSelected() {
    const auto selected = SelectedEntry();
    if (selected && selected->isDir) {
        Browse(selected->path, true);
    }
}

LRESULT ClientWindow::HandleCommand(WPARAM wParam, LPARAM) {
    switch (LOWORD(wParam)) {
        case IDC_MODE_USER:
        case IDC_MODE_ANON:
            UpdateLoginMode();
            return 0;
        case IDC_CONNECT:
            ConnectServer();
            return 0;
        case IDC_SAVE_FAV:
            SaveFavorite();
            return 0;
        case IDC_LOAD_FAV:
            LoadFavoriteSelection();
            return 0;
        case IDC_LOGOUT:
            Logout();
            return 0;
        case IDC_REFRESH:
            Browse(CurrentPath(), false);
            return 0;
        case IDC_BACK:
            Back();
            return 0;
        case IDC_FORWARD:
            Forward();
            return 0;
        case IDC_UP:
            Up();
            return 0;
        case IDC_UPLOAD_FILE:
            UploadFile();
            return 0;
        case IDC_UPLOAD_DIR:
            UploadDirectory();
            return 0;
        case IDC_DOWNLOAD:
            DownloadFile();
            return 0;
        case IDC_DELETE:
            RemoveEntry();
            return 0;
        case IDC_MKDIR:
            MakeDir();
            return 0;
        case IDC_RENAME:
            RenameEntry();
            return 0;
        case IDC_PAUSE:
            PauseTask();
            return 0;
        case IDC_RESUME:
            ResumeTask();
            return 0;
        case IDC_CANCEL:
            CancelTask();
            return 0;
        default:
            return DefWindowProcW(hwnd_, WM_COMMAND, wParam, 0);
    }
}

LRESULT ClientWindow::HandleNotify(LPARAM lParam) {
    const auto* header = reinterpret_cast<LPNMHDR>(lParam);
    if (header->idFrom == IDC_FILES && header->code == NM_DBLCLK) {
        EnterSelected();
    }
    return 0;
}

LRESULT CALLBACK ClientWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ClientWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ClientWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClientWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_CREATE:
            self->BuildUi();
            return 0;
        case WM_TIMER:
            if (wParam == kRefreshTimerId) {
                self->RefreshTasks();
            }
            return 0;
        case WM_NOTIFY:
            return self->HandleNotify(lParam);
        case WM_COMMAND:
            return self->HandleCommand(wParam, lParam);
        case WM_DESTROY:
            KillTimer(hwnd, kRefreshTimerId);
            self->client_.Disconnect();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace fds::clientapp
