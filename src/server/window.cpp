#include "window.hpp"

#include "platform/win32_util.hpp"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <windowsx.h>

namespace fds::serverapp {

namespace {

constexpr int IDC_PORT = 1001;
constexpr int IDC_START = 1002;
constexpr int IDC_STATUS = 1003;
constexpr int IDC_USER_LIST = 1006;
constexpr int IDC_USER_NAME = 1007;
constexpr int IDC_USER_PASS = 1008;
constexpr int IDC_USER_ENABLED = 1011;
constexpr int IDC_USER_ADMIN = 1012;
constexpr int IDC_USER_SAVE = 1013;
constexpr int IDC_USER_DELETE = 1014;
constexpr int IDC_USER_NEW = 1015;
constexpr int IDC_HOME_HINT = 1016;
constexpr int IDC_PERM_BASE = 1100;
constexpr int IDC_DIR_PATH = 1201;
constexpr int IDC_DIR_INPUT = 1202;
constexpr int IDC_DIR_LIST = 1203;
constexpr int IDC_DIR_UP = 1204;
constexpr int IDC_DIR_REFRESH = 1205;
constexpr int IDC_DIR_MAKE = 1206;
constexpr int IDC_DIR_RENAME = 1207;
constexpr int IDC_DIR_DELETE = 1208;
constexpr int IDC_TRANSFERS = 1209;

constexpr UINT_PTR kRefreshTimerId = 1;
constexpr int kBaseWidth = 1280;
constexpr int kBaseHeight = 860;
constexpr int kAreaCount = 4;
constexpr int kPermCount = 4;

constexpr const wchar_t* kAreaLabels[kAreaCount] = {
    L"公共目录",
    L"下载目录",
    L"用户目录",
    L"上传目录",
};

constexpr const wchar_t* kPermLabels[kPermCount] = {
    L"读",
    L"写",
    L"删",
    L"改",
};

constexpr std::uint32_t kPermBits[kPermCount] = {
    PermRead,
    PermWrite,
    PermDelete,
    PermRename,
};

std::string TrimUserName(const std::wstring& text) {
    return Trim(WideToUtf8(text));
}

std::string JoinVirtualPath(const std::string& base, const std::string& name) {
    if (base == "/") {
        return NormalizeVirtualPath("/" + name);
    }
    return NormalizeVirtualPath(base + "/" + name);
}

}  // namespace

ServerWindow::ServerWindow(HINSTANCE instance) : instance_(instance) {}

ServerWindow::~ServerWindow() {
    if (uiFont_) {
        DeleteObject(uiFont_);
    }
    if (titleFont_) {
        DeleteObject(titleFont_);
    }
}

HWND ServerWindow::Place(HWND hwnd, int x, int y, int w, int h) {
    if (!hwnd) {
        return nullptr;
    }
    layoutItems_.push_back({hwnd, RECT{x, y, x + w, y + h}});
    return hwnd;
}

int ServerWindow::Run(int showCmd) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &ServerWindow::WndProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = L"FdsServerWindow";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&windowClass);

    hwnd_ = CreateWindowW(windowClass.lpszClassName, L"FDS 服务端", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          kBaseWidth, kBaseHeight, nullptr, nullptr, instance_, this);
    ShowWindow(hwnd_, showCmd);
    UpdateWindow(hwnd_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void ServerWindow::BuildUi() {
    uiFont_ = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"SimSun");
    titleFont_ = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"SimSun");

    auto makeStatic = [&](const wchar_t* text, int x, int y, int w, int h, HFONT font = nullptr, DWORD style = 0,
                          int id = 0) {
        HWND ctrl = Place(CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd_,
                                        id ? reinterpret_cast<HMENU>(id) : nullptr, instance_, nullptr),
                          x, y, w, h);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : uiFont_), TRUE);
        return ctrl;
    };
    auto makeLine = [&](int x, int y, int w) {
        return Place(CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, x, y, w, 1, hwnd_, nullptr,
                                   instance_, nullptr),
                     x, y, w, 1);
    };
    auto makeEdit = [&](const wchar_t* text, int x, int y, int w, int h, int id, DWORD style = 0) {
        HWND ctrl = Place(CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | style, x, y, w, h, hwnd_,
                                        reinterpret_cast<HMENU>(id), instance_, nullptr),
                          x, y, w, h);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return ctrl;
    };
    auto makeButton = [&](const wchar_t* text, int x, int y, int w, int h, int id, DWORD style = 0) {
        HWND ctrl = Place(CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd_,
                                        reinterpret_cast<HMENU>(id), instance_, nullptr),
                          x, y, w, h);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return ctrl;
    };

    makeStatic(L"服务", 32, 24, 80, 34, titleFont_);
    makeLine(120, 42, 1118);
    makeStatic(L"端口", 40, 78, 70, 24);
    portEdit_ = makeEdit(L"9527", 40, 108, 180, 36, IDC_PORT);
    startBtn_ = makeButton(L"启动服务", 240, 108, 128, 38, IDC_START);
    makeStatic(L"状态", 404, 78, 70, 24);
    status_ = makeStatic(L"", 404, 112, 820, 28, nullptr, 0, IDC_STATUS);

    makeStatic(L"用户", 32, 182, 80, 34, titleFont_);
    makeLine(120, 198, 186);
    userList_ = Place(CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 40, 224,
                                    266, 370, hwnd_, reinterpret_cast<HMENU>(IDC_USER_LIST), instance_, nullptr),
                      40, 224, 266, 370);
    SendMessageW(userList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    newBtn_ = makeButton(L"新建", 40, 614, 126, 38, IDC_USER_NEW);
    deleteBtn_ = makeButton(L"删除", 180, 614, 126, 38, IDC_USER_DELETE);

    makeStatic(L"信息", 334, 182, 80, 34, titleFont_);
    makeLine(420, 198, 230);
    makeStatic(L"用户名", 334, 224, 90, 24);
    userName_ = makeEdit(L"", 334, 254, 260, 36, IDC_USER_NAME);
    makeStatic(L"密码", 334, 306, 90, 24);
    userPass_ = makeEdit(L"", 334, 336, 260, 36, IDC_USER_PASS, ES_PASSWORD);
    userEnabled_ = makeButton(L"启用", 334, 392, 92, 28, IDC_USER_ENABLED, BS_AUTOCHECKBOX);
    userAdmin_ = makeButton(L"管理员", 442, 392, 118, 28, IDC_USER_ADMIN, BS_AUTOCHECKBOX);
    makeStatic(L"主目录", 334, 442, 90, 24);
    homeHint_ = makeStatic(L"", 334, 472, 300, 28, nullptr, 0, IDC_HOME_HINT);
    makeStatic(L"权限", 334, 520, 80, 34, titleFont_);
    makeLine(420, 536, 230);

    const int permHeaderY = 566;
    const int permRowY = 600;
    const int permLabelWidth = 108;
    const int permButtonWidth = 54;
    const int permGap = 10;
    for (int perm = 0; perm < kPermCount; ++perm) {
        const int x = 334 + permLabelWidth + perm * (permButtonWidth + permGap);
        makeStatic(kPermLabels[perm], x, permHeaderY, permButtonWidth, 22, nullptr, SS_CENTER);
    }
    for (int area = 0; area < kAreaCount; ++area) {
        const int y = permRowY + area * 42;
        makeStatic(kAreaLabels[area], 334, y + 4, permLabelWidth, 24);
        for (int perm = 0; perm < kPermCount; ++perm) {
            const int x = 334 + permLabelWidth + perm * (permButtonWidth + permGap);
            permissionButtons_[area][perm] =
                makeButton(kPermLabels[perm], x, y, permButtonWidth, 28, IDC_PERM_BASE + area * kPermCount + perm,
                           BS_AUTOCHECKBOX | BS_PUSHLIKE);
        }
    }
    saveBtn_ = makeButton(L"保存", 334, 778, 126, 38, IDC_USER_SAVE);

    makeStatic(L"目录", 680, 182, 80, 34, titleFont_);
    makeLine(766, 198, 472);
    makeStatic(L"路径", 680, 224, 70, 24);
    dirPath_ = makeEdit(L"/", 680, 254, 430, 36, IDC_DIR_PATH, ES_READONLY);
    dirUpBtn_ = makeButton(L"上级", 1124, 254, 54, 36, IDC_DIR_UP);
    dirRefreshBtn_ = makeButton(L"刷新", 1188, 254, 54, 36, IDC_DIR_REFRESH);

    dirList_ = Place(CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                   680, 310, 562, 284, hwnd_, reinterpret_cast<HMENU>(IDC_DIR_LIST), instance_, nullptr),
                     680, 310, 562, 284);
    SendMessageW(dirList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    ListView_SetExtendedListViewStyle(dirList_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    win32::AddColumn(dirList_, 0, 170, L"名称");
    win32::AddColumn(dirList_, 1, 70, L"类型");
    win32::AddColumn(dirList_, 2, 90, L"大小");
    win32::AddColumn(dirList_, 3, 140, L"修改时间");
    win32::AddColumn(dirList_, 4, 120, L"路径");

    makeStatic(L"名称", 680, 614, 70, 24);
    dirInput_ = makeEdit(L"", 680, 644, 240, 36, IDC_DIR_INPUT);
    dirMakeBtn_ = makeButton(L"新建", 936, 644, 92, 36, IDC_DIR_MAKE);
    dirRenameBtn_ = makeButton(L"重命名", 1042, 644, 92, 36, IDC_DIR_RENAME);
    dirDeleteBtn_ = makeButton(L"删除", 1148, 644, 94, 36, IDC_DIR_DELETE);

    makeStatic(L"传输", 32, 694, 80, 34, titleFont_);
    makeLine(120, 710, 1118);
    transferList_ =
        Place(CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 40, 736,
                            1202, 80, hwnd_, reinterpret_cast<HMENU>(IDC_TRANSFERS), instance_, nullptr),
              40, 736, 1202, 80);
    SendMessageW(transferList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    ListView_SetExtendedListViewStyle(transferList_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    win32::AddColumn(transferList_, 0, 60, L"ID");
    win32::AddColumn(transferList_, 1, 100, L"用户");
    win32::AddColumn(transferList_, 2, 80, L"类型");
    win32::AddColumn(transferList_, 3, 300, L"路径");
    win32::AddColumn(transferList_, 4, 160, L"进度");
    win32::AddColumn(transferList_, 5, 100, L"状态");
    win32::AddColumn(transferList_, 6, 220, L"说明");
    win32::AddColumn(transferList_, 7, 130, L"更新时间");

    CreateNewUser();
    ReloadUsers();
    RefreshStatus();
    RefreshDirectory();
    RefreshTransfers();
    LayoutControls();
    SetTimer(hwnd_, kRefreshTimerId, 800, nullptr);
}

void ServerWindow::LayoutControls() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const double scaleX = std::max(0.4, static_cast<double>(client.right) / static_cast<double>(kBaseWidth));
    const double scaleY = std::max(0.4, static_cast<double>(client.bottom) / static_cast<double>(kBaseHeight));

    for (const auto& item : layoutItems_) {
        const int x = static_cast<int>(std::lround(item.rect.left * scaleX));
        const int y = static_cast<int>(std::lround(item.rect.top * scaleY));
        const int w = std::max(1, static_cast<int>(std::lround((item.rect.right - item.rect.left) * scaleX)));
        const int h = std::max(1, static_cast<int>(std::lround((item.rect.bottom - item.rect.top) * scaleY)));
        MoveWindow(item.hwnd, x, y, w, h, TRUE);
    }

    ResizeListColumns();
}

void ServerWindow::ResizeListColumns() {
    RECT rc{};
    GetClientRect(dirList_, &rc);
    const int dirWidth = std::max(420L, rc.right - rc.left);
    ListView_SetColumnWidth(dirList_, 0, static_cast<int>(dirWidth * 0.24));
    ListView_SetColumnWidth(dirList_, 1, static_cast<int>(dirWidth * 0.12));
    ListView_SetColumnWidth(dirList_, 2, static_cast<int>(dirWidth * 0.14));
    ListView_SetColumnWidth(dirList_, 3, static_cast<int>(dirWidth * 0.25));
    ListView_SetColumnWidth(dirList_, 4, std::max(90, dirWidth - static_cast<int>(dirWidth * 0.75)));

    GetClientRect(transferList_, &rc);
    const int transferWidth = std::max(900L, rc.right - rc.left);
    ListView_SetColumnWidth(transferList_, 0, 60);
    ListView_SetColumnWidth(transferList_, 1, 100);
    ListView_SetColumnWidth(transferList_, 2, 80);
    ListView_SetColumnWidth(transferList_, 3, static_cast<int>(transferWidth * 0.27));
    ListView_SetColumnWidth(transferList_, 4, static_cast<int>(transferWidth * 0.14));
    ListView_SetColumnWidth(transferList_, 5, 90);
    ListView_SetColumnWidth(transferList_, 6, static_cast<int>(transferWidth * 0.22));
    ListView_SetColumnWidth(transferList_, 7, static_cast<int>(transferWidth * 0.12));
}

void ServerWindow::RefreshStatus() {
    win32::SetText(status_, core_.StatusText());
    SetWindowTextW(startBtn_, core_.IsRunning() ? L"停止服务" : L"启动服务");
}

void ServerWindow::RefreshTransfers() {
    const auto transfers = core_.SnapshotTransfers();
    ListView_DeleteAllItems(transferList_);

    for (int i = 0; i < static_cast<int>(transfers.size()); ++i) {
        const auto& item = transfers[i];
        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = i;
        auto id = Utf8ToWide(std::to_string(item.id));
        row.pszText = id.data();
        ListView_InsertItem(transferList_, &row);

        auto user = Utf8ToWide(item.username);
        auto direction = Utf8ToWide(item.direction);
        auto path = Utf8ToWide(item.path);
        const std::string totalText = item.total == 0 ? "-" : FormatBytes(item.total);
        auto progress = Utf8ToWide(FormatBytes(item.done)) + L" / " + Utf8ToWide(totalText);
        auto status = Utf8ToWide(item.status);
        auto detail = Utf8ToWide(item.detail);
        auto updatedAt = Utf8ToWide(item.updatedAt);

        ListView_SetItemText(transferList_, i, 1, user.data());
        ListView_SetItemText(transferList_, i, 2, direction.data());
        ListView_SetItemText(transferList_, i, 3, path.data());
        ListView_SetItemText(transferList_, i, 4, progress.data());
        ListView_SetItemText(transferList_, i, 5, status.data());
        ListView_SetItemText(transferList_, i, 6, detail.data());
        ListView_SetItemText(transferList_, i, 7, updatedAt.data());
    }
}

void ServerWindow::RefreshDirectory(const std::wstring& preferredSelection) {
    std::wstring target = preferredSelection;
    if (target.empty()) {
        const int selected = ListView_GetNextItem(dirList_, -1, LVNI_SELECTED);
        if (selected >= 0 && selected < static_cast<int>(dirEntries_.size())) {
            target = Utf8ToWide(dirEntries_[selected].name);
        }
    }

    std::string cwd;
    std::string error;
    auto entries = core_.SnapshotAdminDirectory(CurrentDirPath(), cwd, error);
    if (!error.empty()) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    dirEntries_ = std::move(entries);
    win32::SetText(dirPath_, Utf8ToWide(cwd));
    ListView_DeleteAllItems(dirList_);

    int selectedIndex = -1;
    for (int i = 0; i < static_cast<int>(dirEntries_.size()); ++i) {
        const auto& entry = dirEntries_[i];
        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = i;

        auto name = Utf8ToWide(entry.name);
        row.pszText = name.data();
        ListView_InsertItem(dirList_, &row);

        auto type = std::wstring(entry.isDir ? L"目录" : L"文件");
        auto size = Utf8ToWide(entry.isDir ? "-" : FormatBytes(entry.size));
        auto time = Utf8ToWide(entry.mtime);
        auto path = Utf8ToWide(entry.path);

        ListView_SetItemText(dirList_, i, 1, type.data());
        ListView_SetItemText(dirList_, i, 2, size.data());
        ListView_SetItemText(dirList_, i, 3, time.data());
        ListView_SetItemText(dirList_, i, 4, path.data());

        if (!target.empty() && target == name) {
            selectedIndex = i;
        }
    }

    if (selectedIndex >= 0) {
        ListView_SetItemState(dirList_, selectedIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void ServerWindow::ReloadUsers(const std::wstring& preferredName) {
    std::wstring selectedName = preferredName;
    if (selectedName.empty()) {
        const int currentSelection = ListBox_GetCurSel(userList_);
        if (currentSelection != LB_ERR) {
            wchar_t buffer[256]{};
            ListBox_GetText(userList_, currentSelection, buffer);
            selectedName = buffer;
        }
    }

    const auto users = core_.SnapshotUsers();
    SendMessageW(userList_, LB_RESETCONTENT, 0, 0);
    if (users.empty()) {
        CreateNewUser();
        return;
    }

    int selectedIndex = 0;
    for (int i = 0; i < static_cast<int>(users.size()); ++i) {
        const auto name = Utf8ToWide(users[i].username);
        ListBox_AddString(userList_, name.c_str());
        if (!selectedName.empty() && name == selectedName) {
            selectedIndex = i;
        }
    }

    ListBox_SetCurSel(userList_, selectedIndex);
    ApplyUser(users[selectedIndex]);
}

void ServerWindow::ClearUserFields() {
    win32::SetText(userName_, L"");
    win32::SetText(userPass_, L"");
    Button_SetCheck(userEnabled_, BST_CHECKED);
    Button_SetCheck(userAdmin_, BST_UNCHECKED);
    SetPermissionDefaults();
    UpdateHomeHint();
    UpdatePermissionButtons();
}

void ServerWindow::ApplyUser(const UserRecord& user) {
    win32::SetText(userName_, Utf8ToWide(user.username));
    win32::SetText(userPass_, L"");
    Button_SetCheck(userEnabled_, user.enabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(userAdmin_, user.admin ? BST_CHECKED : BST_UNCHECKED);

    for (auto& row : permissionButtons_) {
        for (HWND button : row) {
            Button_SetCheck(button, BST_UNCHECKED);
        }
    }

    if (user.admin) {
        for (auto& row : permissionButtons_) {
            for (HWND button : row) {
                Button_SetCheck(button, BST_CHECKED);
            }
        }
    } else {
        const std::array<std::string, kAreaCount> paths = {
            "/public",
            "/download",
            "/users/" + user.username,
            "/upload/" + user.username,
        };
        for (int area = 0; area < kAreaCount; ++area) {
            for (int perm = 0; perm < kPermCount; ++perm) {
                const bool allowed = HasPermission(user.rules, paths[area], kPermBits[perm]);
                Button_SetCheck(permissionButtons_[area][perm], allowed ? BST_CHECKED : BST_UNCHECKED);
            }
        }
    }

    UpdateHomeHint();
    UpdatePermissionButtons();
}

void ServerWindow::FillFromSelection() {
    const int index = ListBox_GetCurSel(userList_);
    if (index == LB_ERR) {
        CreateNewUser();
        return;
    }

    wchar_t buffer[256]{};
    ListBox_GetText(userList_, index, buffer);
    const auto target = std::wstring(buffer);
    for (const auto& user : core_.SnapshotUsers()) {
        if (Utf8ToWide(user.username) == target) {
            ApplyUser(user);
            return;
        }
    }
}

void ServerWindow::CreateNewUser() {
    SendMessageW(userList_, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    ClearUserFields();
    SetFocus(userName_);
}

void ServerWindow::ToggleServer() {
    if (core_.IsRunning()) {
        core_.Stop();
        RefreshStatus();
        return;
    }

    const int port = _wtoi(win32::GetText(portEdit_).c_str());
    std::string error;
    if (!core_.Start(port > 0 ? port : kDefaultPort, error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    RefreshStatus();
}

void ServerWindow::SaveUser() {
    const std::string username = TrimUserName(win32::GetText(userName_));
    if (username.empty()) {
        AlertUser(L"请先填写用户名");
        return;
    }
    if (username == "." || username == ".." || username.find_first_of("\\/\t\r\n") != std::string::npos) {
        AlertUser(L"用户名不能包含斜杠或非法路径字符");
        return;
    }

    const bool admin = Button_GetCheck(userAdmin_) == BST_CHECKED;
    const std::string ruleSpec = BuildRuleSpec(username, admin);
    const auto rules = ParseRuleSpec(ruleSpec);
    if (!admin && rules.empty()) {
        AlertUser(L"普通用户至少保留一项权限");
        return;
    }
    if (!admin && !HasPermission(rules, "/public", PermRead) && !HasPermission(rules, "/download", PermRead) &&
        !HasPermission(rules, "/users/" + username, PermRead) &&
        !HasPermission(rules, "/upload/" + username, PermRead)) {
        AlertUser(L"普通用户至少保留一个读取权限");
        return;
    }

    UserRecord user;
    user.username = username;
    user.home = SuggestedHome(username, admin);
    user.ruleSpec = ruleSpec;
    user.enabled = Button_GetCheck(userEnabled_) == BST_CHECKED;
    user.admin = admin;

    std::string error;
    if (!core_.UpsertUser(user, WideToUtf8(win32::GetText(userPass_)), error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    ReloadUsers(Utf8ToWide(username));
    RefreshStatus();
}

void ServerWindow::RemoveUser() {
    const std::string username = TrimUserName(win32::GetText(userName_));
    if (username.empty()) {
        AlertUser(L"请先选择要删除的用户");
        return;
    }

    std::string error;
    if (!core_.DeleteUser(username, error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    ReloadUsers();
    RefreshStatus();
}

void ServerWindow::UpdatePermissionButtons() {
    const bool admin = Button_GetCheck(userAdmin_) == BST_CHECKED;
    for (auto& row : permissionButtons_) {
        for (HWND button : row) {
            if (admin) {
                Button_SetCheck(button, BST_CHECKED);
            }
            EnableWindow(button, !admin);
        }
    }
    UpdateHomeHint();
}

void ServerWindow::UpdateHomeHint() {
    const std::string username = TrimUserName(win32::GetText(userName_));
    const bool admin = Button_GetCheck(userAdmin_) == BST_CHECKED;
    const std::string home = SuggestedHome(username.empty() ? "new_user" : username, admin);
    win32::SetText(homeHint_, Utf8ToWide(home));
}

void ServerWindow::SetPermissionDefaults() {
    for (auto& row : permissionButtons_) {
        for (HWND button : row) {
            Button_SetCheck(button, BST_UNCHECKED);
        }
    }

    Button_SetCheck(permissionButtons_[0][0], BST_CHECKED);
    Button_SetCheck(permissionButtons_[1][0], BST_CHECKED);
    for (int perm = 0; perm < kPermCount; ++perm) {
        Button_SetCheck(permissionButtons_[2][perm], BST_CHECKED);
        Button_SetCheck(permissionButtons_[3][perm], BST_CHECKED);
    }
}

std::string ServerWindow::SuggestedHome(const std::string& username, bool admin) const {
    if (admin) {
        return "/";
    }

    const auto userPath = NormalizeVirtualPath("/users/" + username);
    const auto uploadPath = NormalizeVirtualPath("/upload/" + username);
    if (Button_GetCheck(permissionButtons_[2][0]) == BST_CHECKED) {
        return userPath;
    }
    if (Button_GetCheck(permissionButtons_[3][0]) == BST_CHECKED) {
        return uploadPath;
    }
    if (Button_GetCheck(permissionButtons_[0][0]) == BST_CHECKED) {
        return "/public";
    }
    if (Button_GetCheck(permissionButtons_[1][0]) == BST_CHECKED) {
        return "/download";
    }
    return userPath;
}

std::string ServerWindow::BuildRuleSpec(const std::string& username, bool admin) const {
    if (admin) {
        return "/:RWDN";
    }

    const std::array<std::string, kAreaCount> paths = {
        "/public",
        "/download",
        "/users/" + username,
        "/upload/" + username,
    };

    std::vector<std::string> parts;
    for (int area = 0; area < kAreaCount; ++area) {
        std::uint32_t bits = 0;
        for (int perm = 0; perm < kPermCount; ++perm) {
            if (Button_GetCheck(permissionButtons_[area][perm]) == BST_CHECKED) {
                bits |= kPermBits[perm];
            }
        }
        if (bits != 0) {
            parts.push_back(paths[area] + ":" + FormatPermBits(bits));
        }
    }

    std::ostringstream spec;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) {
            spec << ';';
        }
        spec << parts[i];
    }
    return spec.str();
}

std::string ServerWindow::CurrentDirPath() const {
    const auto path = WideToUtf8(win32::GetText(dirPath_));
    return path.empty() ? "/" : path;
}

std::optional<FileEntry> ServerWindow::SelectedDirectoryEntry() const {
    const int index = ListView_GetNextItem(dirList_, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(dirEntries_.size())) {
        return std::nullopt;
    }
    return dirEntries_[index];
}

void ServerWindow::DirectoryUp() {
    const auto current = NormalizeVirtualPath(CurrentDirPath());
    if (current.empty() || current == "/") {
        return;
    }

    const auto pos = current.find_last_of('/');
    const auto parent = pos <= 0 ? "/" : current.substr(0, pos);
    win32::SetText(dirPath_, Utf8ToWide(parent));
    RefreshDirectory();
}

void ServerWindow::DirectoryMake() {
    const auto name = Trim(WideToUtf8(win32::GetText(dirInput_)));
    if (name.empty()) {
        AlertUser(L"请输入目录名称");
        return;
    }

    std::string error;
    if (!core_.AdminMakeDir(JoinVirtualPath(CurrentDirPath(), name), error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }
    RefreshDirectory(Utf8ToWide(name));
}

void ServerWindow::DirectoryRename() {
    const auto selected = SelectedDirectoryEntry();
    if (!selected) {
        AlertUser(L"请先选择文件或目录");
        return;
    }

    const auto newName = Trim(WideToUtf8(win32::GetText(dirInput_)));
    if (newName.empty()) {
        AlertUser(L"请输入新名称");
        return;
    }

    std::string error;
    if (!core_.AdminRename(selected->path, newName, error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }
    RefreshDirectory(Utf8ToWide(newName));
}

void ServerWindow::DirectoryDelete() {
    const auto selected = SelectedDirectoryEntry();
    if (!selected) {
        AlertUser(L"请先选择文件或目录");
        return;
    }

    std::string error;
    if (!core_.AdminRemove(selected->path, error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }
    RefreshDirectory();
}

void ServerWindow::OpenSelectedDirectory() {
    const auto selected = SelectedDirectoryEntry();
    if (selected && selected->isDir) {
        win32::SetText(dirPath_, Utf8ToWide(selected->path));
        RefreshDirectory();
    }
}

void ServerWindow::AlertUser(const std::wstring& text) const {
    win32::Alert(hwnd_, L"FDS 服务端", text);
}

LRESULT CALLBACK ServerWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ServerWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ServerWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ServerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_CREATE:
            self->BuildUi();
            return 0;
        case WM_SIZE:
            self->LayoutControls();
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, (control == self->status_ || control == self->homeHint_) ? RGB(48, 48, 48)
                                                                                       : RGB(18, 18, 18));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(18, 18, 18));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_TIMER:
            if (wParam == kRefreshTimerId) {
                self->RefreshStatus();
                self->RefreshTransfers();
            }
            return 0;
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<LPNMHDR>(lParam);
            if (header->idFrom == IDC_DIR_LIST && header->code == NM_DBLCLK) {
                self->OpenSelectedDirectory();
            }
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_START:
                    self->ToggleServer();
                    return 0;
                case IDC_USER_NEW:
                    self->CreateNewUser();
                    return 0;
                case IDC_USER_SAVE:
                    self->SaveUser();
                    return 0;
                case IDC_USER_DELETE:
                    self->RemoveUser();
                    return 0;
                case IDC_DIR_UP:
                    self->DirectoryUp();
                    return 0;
                case IDC_DIR_REFRESH:
                    self->RefreshDirectory();
                    return 0;
                case IDC_DIR_MAKE:
                    self->DirectoryMake();
                    return 0;
                case IDC_DIR_RENAME:
                    self->DirectoryRename();
                    return 0;
                case IDC_DIR_DELETE:
                    self->DirectoryDelete();
                    return 0;
                case IDC_USER_LIST:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        self->FillFromSelection();
                    }
                    return 0;
                case IDC_USER_NAME:
                    if (HIWORD(wParam) == EN_CHANGE) {
                        self->UpdateHomeHint();
                    }
                    return 0;
                case IDC_USER_ADMIN:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        self->UpdatePermissionButtons();
                    }
                    return 0;
                default:
                    if (LOWORD(wParam) >= IDC_PERM_BASE && LOWORD(wParam) < IDC_PERM_BASE + kAreaCount * kPermCount) {
                        self->UpdateHomeHint();
                        return 0;
                    }
                    return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
        case WM_DESTROY:
            KillTimer(hwnd, kRefreshTimerId);
            self->core_.Stop();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace fds::serverapp
