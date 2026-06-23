#include "window.hpp"

#include "platform/win32_util.hpp"

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

constexpr UINT_PTR kRefreshTimerId = 1;

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

int ServerWindow::Run(int showCmd) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &ServerWindow::WndProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = L"FdsServerWindow";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&windowClass);

    hwnd_ = CreateWindowW(windowClass.lpszClassName, L"FDS 服务端", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          1120, 760, nullptr, nullptr, instance_, this);
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
    uiFont_ = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"SimSun");
    titleFont_ = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"SimSun");

    auto createText = [&](const wchar_t* text, int x, int y, int w, int h, HFONT font = nullptr, DWORD style = 0,
                          int id = 0) {
        HWND ctrl = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd_,
                                  id ? reinterpret_cast<HMENU>(id) : nullptr, instance_, nullptr);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : uiFont_), TRUE);
        return ctrl;
    };
    auto createLine = [&](int x, int y, int w) {
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, x, y, w, 1, hwnd_, nullptr, instance_,
                      nullptr);
    };
    auto createEdit = [&](const wchar_t* text, int x, int y, int w, int h, int id, DWORD extraStyle = 0) {
        HWND ctrl = CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | extraStyle, x, y, w, h, hwnd_,
                                  reinterpret_cast<HMENU>(id), instance_, nullptr);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return ctrl;
    };
    auto createButton = [&](const wchar_t* text, int x, int y, int w, int h, int id, DWORD extraStyle = 0) {
        HWND ctrl = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | extraStyle, x, y, w, h, hwnd_,
                                  reinterpret_cast<HMENU>(id), instance_, nullptr);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return ctrl;
    };

    createText(L"服务", 32, 24, 70, 30, titleFont_);
    createLine(104, 40, 960);
    createText(L"端口", 40, 76, 60, 22);
    portEdit_ = createEdit(L"9527", 40, 104, 150, 32, IDC_PORT);
    startBtn_ = createButton(L"启动服务", 210, 104, 120, 34, IDC_START, BS_PUSHBUTTON);
    createText(L"状态", 366, 76, 60, 22);
    status_ = createText(L"", 366, 106, 680, 24, nullptr, 0, IDC_STATUS);

    createText(L"用户", 32, 172, 70, 30, titleFont_);
    createLine(104, 188, 230);
    userList_ = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 40, 218, 276,
                              386, hwnd_, reinterpret_cast<HMENU>(IDC_USER_LIST), instance_, nullptr);
    SendMessageW(userList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    newBtn_ = createButton(L"新建", 40, 622, 128, 34, IDC_USER_NEW, BS_PUSHBUTTON);
    deleteBtn_ = createButton(L"删除", 188, 622, 128, 34, IDC_USER_DELETE, BS_PUSHBUTTON);

    createText(L"信息", 360, 172, 70, 30, titleFont_);
    createLine(432, 188, 632);

    createText(L"用户名", 360, 218, 90, 22);
    userName_ = createEdit(L"", 360, 246, 240, 32, IDC_USER_NAME);
    createText(L"密码", 636, 218, 90, 22);
    userPass_ = createEdit(L"", 636, 246, 240, 32, IDC_USER_PASS, ES_PASSWORD);

    userEnabled_ = createButton(L"启用", 360, 298, 80, 24, IDC_USER_ENABLED, BS_AUTOCHECKBOX);
    userAdmin_ = createButton(L"管理员", 470, 298, 100, 24, IDC_USER_ADMIN, BS_AUTOCHECKBOX);

    createText(L"主目录", 360, 344, 90, 22);
    homeHint_ = createText(L"", 360, 372, 520, 24, nullptr, 0, IDC_HOME_HINT);

    createText(L"权限", 360, 430, 70, 30, titleFont_);
    createLine(432, 446, 632);

    const int gridLeft = 360;
    const int headerTop = 478;
    const int rowTop = 510;
    const int rowHeight = 48;
    const int labelWidth = 110;
    const int buttonWidth = 58;
    const int buttonGap = 16;

    for (int perm = 0; perm < kPermCount; ++perm) {
        const int x = gridLeft + labelWidth + perm * (buttonWidth + buttonGap);
        createText(kPermLabels[perm], x, headerTop, buttonWidth, 22, nullptr, SS_CENTER);
    }

    for (int area = 0; area < kAreaCount; ++area) {
        const int y = rowTop + area * rowHeight;
        createText(kAreaLabels[area], gridLeft, y + 6, labelWidth, 24);
        for (int perm = 0; perm < kPermCount; ++perm) {
            const int x = gridLeft + labelWidth + perm * (buttonWidth + buttonGap);
            permissionButtons_[area][perm] =
                createButton(kPermLabels[perm], x, y, buttonWidth, 30, IDC_PERM_BASE + area * kPermCount + perm,
                             BS_AUTOCHECKBOX | BS_PUSHLIKE | WS_TABSTOP);
        }
    }

    saveBtn_ = createButton(L"保存", 360, 622, 140, 36, IDC_USER_SAVE, BS_PUSHBUTTON);

    CreateNewUser();
    ReloadUsers();
    RefreshStatus();
    SetTimer(hwnd_, kRefreshTimerId, 1000, nullptr);
}

void ServerWindow::RefreshStatus() {
    win32::SetText(status_, core_.StatusText());
    SetWindowTextW(startBtn_, core_.IsRunning() ? L"停止服务" : L"启动服务");
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
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(hdc, TRANSPARENT);
            if (control == self->status_ || control == self->homeHint_) {
                SetTextColor(hdc, RGB(90, 90, 90));
            } else {
                SetTextColor(hdc, RGB(32, 32, 32));
            }
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(32, 32, 32));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_TIMER:
            if (wParam == kRefreshTimerId) {
                self->RefreshStatus();
            }
            return 0;
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
