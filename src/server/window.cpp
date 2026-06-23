#include "window.hpp"

#include "platform/win32_util.hpp"

#include <algorithm>
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
constexpr int kMinTrackWidth = 1180;
constexpr int kMinTrackHeight = 820;
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

// 读取并清理用户输入框中的用户名。
std::string TrimUserName(const std::wstring& text) {
    return Trim(WideToUtf8(text));
}

// 拼接管理员目录管理使用的逻辑路径。
std::string JoinVirtualPath(const std::string& base, const std::string& name) {
    if (base == "/") {
        return NormalizeVirtualPath("/" + name);
    }
    return NormalizeVirtualPath(base + "/" + name);
}

}  // namespace

// 保存应用实例句柄。
ServerWindow::ServerWindow(HINSTANCE instance) : instance_(instance) {}

// 释放界面字体资源。
ServerWindow::~ServerWindow() {
    if (uiFont_) {
        DeleteObject(uiFont_);
    }
    if (titleFont_) {
        DeleteObject(titleFont_);
    }
}

// 记录控件原始布局位置，供窗口缩放时统一重排。
HWND ServerWindow::Place(HWND hwnd, int x, int y, int w, int h) {
    if (!hwnd) {
        return nullptr;
    }
    layoutItems_.push_back({hwnd, RECT{x, y, x + w, y + h}});
    return hwnd;
}

// 创建主窗口并进入标准 Win32 消息循环。
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

// 构建服务端管理员面板中的全部控件。
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

    serviceTitle_ = makeStatic(L"服务", 24, 24, 80, 34, titleFont_);
    serviceLine_ = makeLine(112, 42, 1130);
    portLabel_ = makeStatic(L"端口", 24, 76, 54, 24);
    portEdit_ = makeEdit(L"9527", 82, 70, 120, 34, IDC_PORT);
    startBtn_ = makeButton(L"启动服务", 214, 68, 124, 36, IDC_START);
    statusLabel_ = makeStatic(L"状态", 24, 118, 54, 24);
    status_ = makeStatic(L"", 82, 118, 1160, 26, nullptr, 0, IDC_STATUS);

    userTitle_ = makeStatic(L"用户", 24, 166, 80, 34, titleFont_);
    userLine_ = makeLine(112, 184, 180);
    userList_ = Place(CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 40, 224,
                                    266, 370, hwnd_, reinterpret_cast<HMENU>(IDC_USER_LIST), instance_, nullptr),
                      40, 224, 266, 370);
    SendMessageW(userList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    newBtn_ = makeButton(L"新建", 40, 614, 126, 38, IDC_USER_NEW);
    deleteBtn_ = makeButton(L"删除", 180, 614, 126, 38, IDC_USER_DELETE);

    infoTitle_ = makeStatic(L"账号", 334, 166, 80, 34, titleFont_);
    infoLine_ = makeLine(422, 184, 230);
    userNameLabel_ = makeStatic(L"用户", 334, 224, 90, 24);
    userName_ = makeEdit(L"", 334, 254, 260, 36, IDC_USER_NAME);
    userPassLabel_ = makeStatic(L"密码", 334, 306, 90, 24);
    userPass_ = makeEdit(L"", 334, 336, 260, 36, IDC_USER_PASS, ES_PASSWORD);
    userEnabled_ = makeButton(L"启用", 334, 392, 92, 28, IDC_USER_ENABLED, BS_AUTOCHECKBOX);
    userAdmin_ = makeButton(L"管理员", 442, 392, 118, 28, IDC_USER_ADMIN, BS_AUTOCHECKBOX);
    homeLabel_ = makeStatic(L"主目录", 334, 442, 90, 24);
    homeHint_ = makeStatic(L"", 334, 472, 300, 28, nullptr, 0, IDC_HOME_HINT);
    permTitle_ = makeStatic(L"权限", 334, 520, 80, 34, titleFont_);
    permLine_ = makeLine(422, 536, 230);
    for (int perm = 0; perm < kPermCount; ++perm) {
        permHeaderLabels_[perm] = makeStatic(kPermLabels[perm], 334, 566, 54, 22, nullptr, SS_CENTER);
    }
    for (int area = 0; area < kAreaCount; ++area) {
        permAreaLabels_[area] = makeStatic(kAreaLabels[area], 334, 600, 108, 24);
        for (int perm = 0; perm < kPermCount; ++perm) {
            permissionButtons_[area][perm] =
                makeButton(kPermLabels[perm], 446, 600, 54, 28, IDC_PERM_BASE + area * kPermCount + perm,
                           BS_AUTOCHECKBOX | BS_PUSHLIKE);
        }
    }
    saveBtn_ = makeButton(L"保存", 334, 778, 126, 38, IDC_USER_SAVE);

    dirTitle_ = makeStatic(L"目录", 680, 166, 80, 34, titleFont_);
    dirLine_ = makeLine(768, 184, 474);
    dirPathLabel_ = makeStatic(L"路径", 680, 224, 70, 24);
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

    dirNameLabel_ = makeStatic(L"名称", 680, 614, 70, 24);
    dirInput_ = makeEdit(L"", 680, 644, 240, 36, IDC_DIR_INPUT);
    dirMakeBtn_ = makeButton(L"新建", 936, 644, 92, 36, IDC_DIR_MAKE);
    dirRenameBtn_ = makeButton(L"重命名", 1042, 644, 92, 36, IDC_DIR_RENAME);
    dirDeleteBtn_ = makeButton(L"删除", 1148, 644, 94, 36, IDC_DIR_DELETE);

    transferTitle_ = makeStatic(L"传输", 24, 694, 80, 34, titleFont_);
    transferLine_ = makeLine(112, 712, 1130);
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

// 按当前窗口尺寸重新计算各区域布局，避免控件重叠。
void ServerWindow::LayoutControls() {
    RECT client{};
    GetClientRect(hwnd_, &client);

    const int clientWidth = std::max(800L, client.right - client.left);
    const int clientHeight = std::max(700L, client.bottom - client.top);
    const int margin = 24;
    const int gap = 18;
    const int titleWidth = 72;
    const int titleHeight = 32;
    const int labelHeight = 22;
    const int editHeight = 34;
    const int buttonHeight = 36;
    const int smallGap = 8;
    const int sectionTopHeight = 124;
    const int bottomSectionHeight = std::clamp(clientHeight / 4, 180, 240);
    const int middleTop = margin + sectionTopHeight + gap;
    const int bottomTop = clientHeight - margin - bottomSectionHeight;
    const int middleHeight = std::max(360, bottomTop - gap - middleTop);
    const int availableWidth = clientWidth - margin * 2 - gap * 2;

    int leftWidth = std::clamp(availableWidth / 5, 220, 280);
    int middleWidth = std::clamp(static_cast<int>(availableWidth * 0.30), 320, 390);
    int rightWidth = availableWidth - leftWidth - middleWidth;
    if (rightWidth < 360) {
        const int need = 360 - rightWidth;
        const int cutMiddle = std::min(need, std::max(0, middleWidth - 300));
        middleWidth -= cutMiddle;
        rightWidth += cutMiddle;
    }
    if (rightWidth < 360) {
        const int need = 360 - rightWidth;
        const int cutLeft = std::min(need, std::max(0, leftWidth - 210));
        leftWidth -= cutLeft;
        rightWidth += cutLeft;
    }

    const int leftX = margin;
    const int middleX = leftX + leftWidth + gap;
    const int rightX = middleX + middleWidth + gap;

    auto move = [](HWND hwnd, int x, int y, int w, int h) {
        if (!hwnd) {
            return;
        }
        MoveWindow(hwnd, x, y, std::max(1, w), std::max(1, h), TRUE);
    };

    move(serviceTitle_, margin, margin, titleWidth, titleHeight);
    move(serviceLine_, margin + 84, margin + 16, clientWidth - margin * 2 - 84, 1);

    const int serviceRow1Y = margin + 48;
    const int serviceRow2Y = serviceRow1Y + 46;
    move(portLabel_, margin, serviceRow1Y + 6, 44, labelHeight);
    move(portEdit_, margin + 50, serviceRow1Y, 124, editHeight);
    move(startBtn_, margin + 188, serviceRow1Y - 1, 120, buttonHeight);
    move(statusLabel_, margin, serviceRow2Y + 4, 44, labelHeight);
    move(status_, margin + 50, serviceRow2Y + 2, clientWidth - margin * 2 - 50, 26);

    move(userTitle_, leftX, middleTop, titleWidth, titleHeight);
    move(userLine_, leftX + 84, middleTop + 16, leftWidth - 84, 1);
    const int userListY = middleTop + 46;
    const int userButtonsY = middleTop + middleHeight - buttonHeight;
    const int userListHeight = std::max(160, userButtonsY - 14 - userListY);
    move(userList_, leftX, userListY, leftWidth, userListHeight);
    const int userButtonGap = 12;
    const int userNewWidth = (leftWidth - userButtonGap) / 2;
    move(newBtn_, leftX, userButtonsY, userNewWidth, buttonHeight);
    move(deleteBtn_, leftX + userNewWidth + userButtonGap, userButtonsY, leftWidth - userNewWidth - userButtonGap,
         buttonHeight);

    move(infoTitle_, middleX, middleTop, titleWidth, titleHeight);
    move(infoLine_, middleX + 84, middleTop + 16, middleWidth - 84, 1);
    const int formLabelWidth = 56;
    const int formValueX = middleX + formLabelWidth + 10;
    const int formValueWidth = middleWidth - formLabelWidth - 10;
    int formY = middleTop + 46;
    move(userNameLabel_, middleX, formY + 6, formLabelWidth, labelHeight);
    move(userName_, formValueX, formY, formValueWidth, editHeight);
    formY += 46;
    move(userPassLabel_, middleX, formY + 6, formLabelWidth, labelHeight);
    move(userPass_, formValueX, formY, formValueWidth, editHeight);
    formY += 46;
    move(userEnabled_, middleX, formY + 4, 86, 24);
    move(userAdmin_, middleX + 98, formY + 4, 116, 24);
    move(saveBtn_, middleX + middleWidth - 100, formY - 2, 100, buttonHeight);
    formY += 42;
    move(homeLabel_, middleX, formY + 4, formLabelWidth, labelHeight);
    move(homeHint_, formValueX, formY + 4, formValueWidth, 24);
    formY += 40;

    move(permTitle_, middleX, formY, titleWidth, titleHeight);
    move(permLine_, middleX + 84, formY + 16, middleWidth - 84, 1);
    const int permHeaderY = formY + 34;
    const int permLabelWidth = 96;
    const int permButtonGap = 8;
    const int permButtonWidth = std::max(48, (middleWidth - permLabelWidth - permButtonGap * 3) / 4);
    const int permStartX = middleX + permLabelWidth;
    for (int perm = 0; perm < kPermCount; ++perm) {
        const int x = permStartX + perm * (permButtonWidth + permButtonGap);
        move(permHeaderLabels_[perm], x, permHeaderY, permButtonWidth, 22);
    }
    const int permRowY = permHeaderY + 26;
    for (int area = 0; area < kAreaCount; ++area) {
        const int y = permRowY + area * 34;
        move(permAreaLabels_[area], middleX, y + 2, permLabelWidth - 8, 24);
        for (int perm = 0; perm < kPermCount; ++perm) {
            const int x = permStartX + perm * (permButtonWidth + permButtonGap);
            move(permissionButtons_[area][perm], x, y, permButtonWidth, 28);
        }
    }

    move(dirTitle_, rightX, middleTop, titleWidth, titleHeight);
    move(dirLine_, rightX + 84, middleTop + 16, rightWidth - 84, 1);

    const bool compactDirHeader = rightWidth < 520;
    const bool stackedDirButtons = rightWidth < 620;
    int dirY = middleTop + 46;
    move(dirPathLabel_, rightX, dirY + 6, 44, labelHeight);
    if (!compactDirHeader) {
        const int pathWidth = rightWidth - 52 - 70 - smallGap - 70;
        move(dirPath_, rightX + 52, dirY, pathWidth, editHeight);
        move(dirUpBtn_, rightX + 52 + pathWidth + smallGap, dirY - 1, 70, buttonHeight);
        move(dirRefreshBtn_, rightX + 52 + pathWidth + smallGap + 70 + smallGap, dirY - 1, 70, buttonHeight);
        dirY += 48;
    } else {
        move(dirPath_, rightX + 52, dirY, rightWidth - 52, editHeight);
        move(dirUpBtn_, rightX, dirY + 42, 90, buttonHeight);
        move(dirRefreshBtn_, rightX + 98, dirY + 42, 90, buttonHeight);
        dirY += 86;
    }

    const int dirFooterTop = middleTop + middleHeight - (stackedDirButtons ? 82 : 38);
    const int dirListHeight = std::max(150, dirFooterTop - 14 - dirY);
    move(dirList_, rightX, dirY, rightWidth, dirListHeight);

    if (!stackedDirButtons) {
        move(dirNameLabel_, rightX, dirFooterTop + 6, 44, labelHeight);
        const int actionWidth = 82 + 96 + 82;
        const int inputWidth = std::max(150, rightWidth - 52 - actionWidth - smallGap * 2);
        const int inputX = rightX + 52;
        move(dirInput_, inputX, dirFooterTop, inputWidth, editHeight);
        move(dirMakeBtn_, inputX + inputWidth + smallGap, dirFooterTop - 1, 82, buttonHeight);
        move(dirRenameBtn_, inputX + inputWidth + smallGap + 82 + smallGap, dirFooterTop - 1, 96, buttonHeight);
        move(dirDeleteBtn_, rightX + rightWidth - 82, dirFooterTop - 1, 82, buttonHeight);
    } else {
        move(dirNameLabel_, rightX, dirFooterTop + 6, 44, labelHeight);
        move(dirInput_, rightX + 52, dirFooterTop, rightWidth - 52, editHeight);
        move(dirMakeBtn_, rightX, dirFooterTop + 44, 90, buttonHeight);
        move(dirRenameBtn_, rightX + 98, dirFooterTop + 44, 106, buttonHeight);
        move(dirDeleteBtn_, rightX + 212, dirFooterTop + 44, 90, buttonHeight);
    }

    move(transferTitle_, margin, bottomTop, titleWidth, titleHeight);
    move(transferLine_, margin + 84, bottomTop + 16, clientWidth - margin * 2 - 84, 1);
    move(transferList_, margin, bottomTop + 46, clientWidth - margin * 2, clientHeight - margin - (bottomTop + 46));

    ResizeListColumns();
}

// 根据列表当前宽度动态调整列宽比例。
void ServerWindow::ResizeListColumns() {
    RECT rc{};
    GetClientRect(dirList_, &rc);
    const int dirWidth = std::max(420L, rc.right - rc.left);
    ListView_SetColumnWidth(dirList_, 0, static_cast<int>(dirWidth * 0.24));
    ListView_SetColumnWidth(dirList_, 1, static_cast<int>(dirWidth * 0.12));
    ListView_SetColumnWidth(dirList_, 2, static_cast<int>(dirWidth * 0.14));
    ListView_SetColumnWidth(dirList_, 3, static_cast<int>(dirWidth * 0.21));
    ListView_SetColumnWidth(dirList_, 4, std::max(100, dirWidth - static_cast<int>(dirWidth * 0.71)));

    GetClientRect(transferList_, &rc);
    const int transferWidth = std::max(900L, rc.right - rc.left);
    ListView_SetColumnWidth(transferList_, 0, 60);
    ListView_SetColumnWidth(transferList_, 1, 100);
    ListView_SetColumnWidth(transferList_, 2, 80);
    ListView_SetColumnWidth(transferList_, 3, static_cast<int>(transferWidth * 0.29));
    ListView_SetColumnWidth(transferList_, 4, static_cast<int>(transferWidth * 0.14));
    ListView_SetColumnWidth(transferList_, 5, 90);
    ListView_SetColumnWidth(transferList_, 6, static_cast<int>(transferWidth * 0.19));
    ListView_SetColumnWidth(transferList_, 7, std::max(120, transferWidth - 60 - 100 - 80 -
                                                                static_cast<int>(transferWidth * 0.29) -
                                                                static_cast<int>(transferWidth * 0.14) - 90 -
                                                                static_cast<int>(transferWidth * 0.19)));
}

// 刷新顶部服务状态和启动/停止按钮文字。
void ServerWindow::RefreshStatus() {
    win32::SetText(status_, core_.StatusText());
    SetWindowTextW(startBtn_, core_.IsRunning() ? L"停止服务" : L"启动服务");
}

// 用最新传输快照刷新底部传输列表。
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

// 刷新管理员目录管理区，并尽量保留原有选中项。
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

// 重新加载用户列表，并选中指定用户名或当前项。
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

// 清空用户表单，回到“新建用户”的初始状态。
void ServerWindow::ClearUserFields() {
    win32::SetText(userName_, L"");
    win32::SetText(userPass_, L"");
    Button_SetCheck(userEnabled_, BST_CHECKED);
    Button_SetCheck(userAdmin_, BST_UNCHECKED);
    SetPermissionDefaults();
    UpdateHomeHint();
    UpdatePermissionButtons();
}

// 把一个用户对象回填到界面表单和权限按钮上。
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

// 根据左侧列表当前选中项切换到对应用户。
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

// 进入“新建用户”模式。
void ServerWindow::CreateNewUser() {
    SendMessageW(userList_, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    ClearUserFields();
    SetFocus(userName_);
}

// 启动或停止服务端。
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

// 读取当前表单内容并保存为用户记录。
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

// 删除当前表单对应的用户。
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

// 管理员勾选时锁定全部权限按钮；普通用户则允许手动配置。
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

// 根据当前权限按钮组合推断一个合适的主目录显示到界面上。
void ServerWindow::UpdateHomeHint() {
    const std::string username = TrimUserName(win32::GetText(userName_));
    const bool admin = Button_GetCheck(userAdmin_) == BST_CHECKED;
    const std::string home = SuggestedHome(username.empty() ? "new_user" : username, admin);
    win32::SetText(homeHint_, Utf8ToWide(home));
}

// 为新建普通用户设置一套默认权限按钮状态。
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

// 根据当前权限按钮组合推断用户登录后的默认主目录。
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

// 把权限按钮状态拼装成 /path:RWDN 形式的规则字符串。
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

// 获取管理员目录管理区当前所在逻辑路径。
std::string ServerWindow::CurrentDirPath() const {
    const auto path = WideToUtf8(win32::GetText(dirPath_));
    return path.empty() ? "/" : path;
}

// 返回目录列表中当前选中的文件项。
std::optional<FileEntry> ServerWindow::SelectedDirectoryEntry() const {
    const int index = ListView_GetNextItem(dirList_, -1, LVNI_SELECTED);
    if (index < 0 || index >= static_cast<int>(dirEntries_.size())) {
        return std::nullopt;
    }
    return dirEntries_[index];
}

// 进入当前目录的上级目录。
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

// 在当前目录下创建子目录。
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

// 重命名当前选中的文件或目录。
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

// 删除当前选中的文件或目录。
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

// 双击目录列表时进入被选中的子目录。
void ServerWindow::OpenSelectedDirectory() {
    const auto selected = SelectedDirectoryEntry();
    if (selected && selected->isDir) {
        win32::SetText(dirPath_, Utf8ToWide(selected->path));
        RefreshDirectory();
    }
}

// 统一弹出管理员面板提示框。
void ServerWindow::AlertUser(const std::wstring& text) const {
    win32::Alert(hwnd_, L"FDS 服务端", text);
}

// 主窗口消息分发：负责按钮点击、列表双击、定时刷新和布局重排。
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
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = kMinTrackWidth;
            info->ptMinTrackSize.y = kMinTrackHeight;
            return 0;
        }
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
