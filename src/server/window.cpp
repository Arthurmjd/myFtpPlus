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
constexpr int IDC_USER_HOME = 1009;
constexpr int IDC_USER_RULES = 1010;
constexpr int IDC_USER_ENABLED = 1011;
constexpr int IDC_USER_ADMIN = 1012;
constexpr int IDC_USER_SAVE = 1013;
constexpr int IDC_USER_DELETE = 1014;
constexpr int IDC_LOGS = 1015;

constexpr UINT_PTR kRefreshTimerId = 1;

}  // namespace

ServerWindow::ServerWindow(HINSTANCE instance) : instance_(instance) {}

int ServerWindow::Run(int showCmd) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &ServerWindow::WndProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = L"FdsServerWindow";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&windowClass);

    hwnd_ = CreateWindowW(windowClass.lpszClassName, L"FDS 服务端 / 管理面板", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                          CW_USEDEFAULT, 980, 680, nullptr, nullptr, instance_, this);
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
    CreateWindowW(L"STATIC", L"监听端口", WS_CHILD | WS_VISIBLE, 20, 20, 80, 24, hwnd_, nullptr, instance_, nullptr);
    portEdit_ = CreateWindowW(L"EDIT", L"9527", WS_CHILD | WS_VISIBLE | WS_BORDER, 100, 18, 90, 24, hwnd_,
                              reinterpret_cast<HMENU>(IDC_PORT), instance_, nullptr);
    startBtn_ = CreateWindowW(L"BUTTON", L"启动服务", WS_CHILD | WS_VISIBLE, 210, 16, 100, 28, hwnd_,
                              reinterpret_cast<HMENU>(IDC_START), instance_, nullptr);
    status_ = CreateWindowW(L"STATIC", L"服务未启动", WS_CHILD | WS_VISIBLE, 330, 20, 600, 24, hwnd_,
                            reinterpret_cast<HMENU>(IDC_STATUS), instance_, nullptr);

    CreateWindowW(L"STATIC", L"管理员面板已直接开启，无需登录", WS_CHILD | WS_VISIBLE, 20, 60, 320, 24, hwnd_, nullptr,
                  instance_, nullptr);

    userList_ = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY, 20, 110, 220, 480, hwnd_,
                              reinterpret_cast<HMENU>(IDC_USER_LIST), instance_, nullptr);
    CreateWindowW(L"STATIC", L"用户名", WS_CHILD | WS_VISIBLE, 260, 110, 70, 24, hwnd_, nullptr, instance_, nullptr);
    userName_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 340, 108, 180, 24, hwnd_,
                              reinterpret_cast<HMENU>(IDC_USER_NAME), instance_, nullptr);
    CreateWindowW(L"STATIC", L"密码", WS_CHILD | WS_VISIBLE, 260, 146, 70, 24, hwnd_, nullptr, instance_, nullptr);
    userPass_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD, 340, 144, 180, 24, hwnd_,
                              reinterpret_cast<HMENU>(IDC_USER_PASS), instance_, nullptr);
    CreateWindowW(L"STATIC", L"主目录", WS_CHILD | WS_VISIBLE, 260, 182, 70, 24, hwnd_, nullptr, instance_, nullptr);
    userHome_ = CreateWindowW(L"EDIT", L"/users/demo", WS_CHILD | WS_VISIBLE | WS_BORDER, 340, 180, 280, 24, hwnd_,
                              reinterpret_cast<HMENU>(IDC_USER_HOME), instance_, nullptr);
    CreateWindowW(L"STATIC", L"权限规则", WS_CHILD | WS_VISIBLE, 260, 218, 70, 24, hwnd_, nullptr, instance_, nullptr);
    userRules_ = CreateWindowW(L"EDIT", L"/public:R;/download:R", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                                                 ES_AUTOVSCROLL,
                               340, 216, 280, 84, hwnd_, reinterpret_cast<HMENU>(IDC_USER_RULES), instance_, nullptr);
    userEnabled_ = CreateWindowW(L"BUTTON", L"启用账户", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 340, 312, 100, 24,
                                 hwnd_, reinterpret_cast<HMENU>(IDC_USER_ENABLED), instance_, nullptr);
    userAdmin_ = CreateWindowW(L"BUTTON", L"管理员", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 450, 312, 100, 24, hwnd_,
                               reinterpret_cast<HMENU>(IDC_USER_ADMIN), instance_, nullptr);
    saveBtn_ = CreateWindowW(L"BUTTON", L"保存/更新用户", WS_CHILD | WS_VISIBLE, 340, 348, 130, 28, hwnd_,
                             reinterpret_cast<HMENU>(IDC_USER_SAVE), instance_, nullptr);
    deleteBtn_ = CreateWindowW(L"BUTTON", L"删除用户", WS_CHILD | WS_VISIBLE, 490, 348, 130, 28, hwnd_,
                               reinterpret_cast<HMENU>(IDC_USER_DELETE), instance_, nullptr);

    CreateWindowW(L"STATIC", L"审计日志", WS_CHILD | WS_VISIBLE, 650, 110, 120, 24, hwnd_, nullptr, instance_, nullptr);
    logs_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
                                       ES_READONLY | WS_VSCROLL,
                          650, 138, 290, 452, hwnd_, reinterpret_cast<HMENU>(IDC_LOGS), instance_, nullptr);

    SetTimer(hwnd_, kRefreshTimerId, 1000, nullptr);
    RefreshUi();
}

void ServerWindow::RefreshUi() {
    win32::SetText(status_, core_.StatusText());
    win32::SetText(logs_, core_.ReadLogs());

    const auto users = core_.SnapshotUsers();
    const int currentSelection = ListBox_GetCurSel(userList_);
    std::wstring selectedName;
    if (currentSelection != LB_ERR) {
        wchar_t buffer[256]{};
        ListBox_GetText(userList_, currentSelection, buffer);
        selectedName = buffer;
    }

    SendMessageW(userList_, LB_RESETCONTENT, 0, 0);
    int newSelection = users.empty() ? LB_ERR : 0;
    for (int i = 0; i < static_cast<int>(users.size()); ++i) {
        const auto name = Utf8ToWide(users[i].username);
        ListBox_AddString(userList_, name.c_str());
        if (name == selectedName) {
            newSelection = i;
        }
    }

    if (newSelection == LB_ERR) {
        ClearUserFields();
        return;
    }

    ListBox_SetCurSel(userList_, newSelection);
    ApplyUser(users[newSelection]);
}

void ServerWindow::ClearUserFields() {
    win32::SetText(userName_, L"");
    win32::SetText(userPass_, L"");
    win32::SetText(userHome_, L"");
    win32::SetText(userRules_, L"");
    Button_SetCheck(userEnabled_, BST_UNCHECKED);
    Button_SetCheck(userAdmin_, BST_UNCHECKED);
}

void ServerWindow::ApplyUser(const UserRecord& user) {
    win32::SetText(userName_, Utf8ToWide(user.username));
    win32::SetText(userPass_, L"");
    win32::SetText(userHome_, Utf8ToWide(user.home));
    win32::SetText(userRules_, Utf8ToWide(user.ruleSpec));
    Button_SetCheck(userEnabled_, user.enabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(userAdmin_, user.admin ? BST_CHECKED : BST_UNCHECKED);
}

void ServerWindow::FillFromSelection() {
    const int index = ListBox_GetCurSel(userList_);
    if (index == LB_ERR) {
        ClearUserFields();
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

void ServerWindow::ToggleServer() {
    if (core_.IsRunning()) {
        core_.Stop();
        SetWindowTextW(startBtn_, L"启动服务");
        RefreshUi();
        return;
    }

    const int port = _wtoi(win32::GetText(portEdit_).c_str());
    std::string error;
    if (!core_.Start(port > 0 ? port : kDefaultPort, error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    SetWindowTextW(startBtn_, L"停止服务");
    RefreshUi();
}

void ServerWindow::SaveUser() {
    UserRecord user;
    user.username = WideToUtf8(win32::GetText(userName_));
    user.home = WideToUtf8(win32::GetText(userHome_));
    user.ruleSpec = WideToUtf8(win32::GetText(userRules_));
    user.enabled = Button_GetCheck(userEnabled_) == BST_CHECKED;
    user.admin = Button_GetCheck(userAdmin_) == BST_CHECKED;

    std::string error;
    if (!core_.UpsertUser(user, WideToUtf8(win32::GetText(userPass_)), error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    RefreshUi();
}

void ServerWindow::RemoveUser() {
    std::string error;
    if (!core_.DeleteUser(WideToUtf8(win32::GetText(userName_)), error)) {
        AlertUser(Utf8ToWide(error));
        return;
    }

    RefreshUi();
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
        case WM_TIMER:
            if (wParam == kRefreshTimerId) {
                self->RefreshUi();
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_START:
                    self->ToggleServer();
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
                default:
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
