#include "common.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>

#include <iostream>
#include <memory>

using namespace fds;

namespace {

constexpr int IDC_HOST = 2001;
constexpr int IDC_PORT = 2002;
constexpr int IDC_USER = 2003;
constexpr int IDC_PASS = 2004;
constexpr int IDC_ANON = 2005;
constexpr int IDC_CONNECT = 2006;
constexpr int IDC_SAVE_FAV = 2007;
constexpr int IDC_FAVORITES = 2008;
constexpr int IDC_LOAD_FAV = 2009;
constexpr int IDC_PATH = 2010;
constexpr int IDC_BACK = 2011;
constexpr int IDC_FORWARD = 2012;
constexpr int IDC_UP = 2013;
constexpr int IDC_REFRESH = 2014;
constexpr int IDC_FILES = 2015;
constexpr int IDC_UPLOAD_FILE = 2016;
constexpr int IDC_UPLOAD_DIR = 2017;
constexpr int IDC_DOWNLOAD = 2018;
constexpr int IDC_DELETE = 2019;
constexpr int IDC_MKDIR = 2020;
constexpr int IDC_RENAME = 2021;
constexpr int IDC_INPUT = 2022;
constexpr int IDC_TASKS = 2023;
constexpr int IDC_PAUSE = 2024;
constexpr int IDC_RESUME = 2025;
constexpr int IDC_CANCEL = 2026;
constexpr int IDC_STATUS = 2027;

struct ScopedSockets {
    ScopedSockets() { InitSockets(); }
    ~ScopedSockets() { CleanupSockets(); }
};

std::wstring GetText(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    GetWindowTextW(hwnd, out.data(), len + 1);
    return out;
}

void SetText(HWND hwnd, const std::wstring& value) {
    SetWindowTextW(hwnd, value.c_str());
}

void Alert(HWND hwnd, const std::wstring& text) {
    MessageBoxW(hwnd, text.c_str(), L"FDS Client", MB_OK | MB_ICONINFORMATION);
}

void AddColumn(HWND list, int index, int width, const wchar_t* title) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.cx = width;
    col.pszText = const_cast<wchar_t*>(title);
    col.iSubItem = index;
    ListView_InsertColumn(list, index, &col);
}

std::wstring PickOpenFile(HWND hwnd) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? std::wstring(file) : L"";
}

std::wstring PickSaveFile(HWND hwnd, const std::wstring& initialName) {
    wchar_t file[MAX_PATH]{};
    wcsncpy_s(file, initialName.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Files\0*.*\0";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return GetSaveFileNameW(&ofn) ? std::wstring(file) : L"";
}

std::wstring PickFolder(HWND hwnd) {
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"选择要上传的目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return L"";
    }
    wchar_t path[MAX_PATH]{};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return path;
}

std::string JoinRemote(const std::string& base, const std::string& name) {
    if (base == "/") {
        return NormalizeVirtualPath("/" + name);
    }
    return NormalizeVirtualPath(base + "/" + name);
}

enum class FlowControl { Continue, Pause, Cancel };

struct ConnectionInfo {
    std::string host;
    int port = kDefaultPort;
    std::uint32_t session = 0;
};

class CommandClient {
public:
    ~CommandClient() { Disconnect(); }

    bool Connect(const std::string& host, int port, const std::string& user, const std::string& password,
                 bool anonymous, std::string& error) {
        Disconnect();
        sock_ = ConnectSocket(host, port);
        if (sock_ == INVALID_SOCKET) {
            error = "连接服务器失败";
            return false;
        }
        host_ = host;
        port_ = port;
        std::map<std::string, std::string> req{
            {"anonymous", anonymous ? "1" : "0"},
            {"username", user},
            {"password", password},
        };
        std::string body;
        if (!Call(Cmd::Login, req, Cmd::LoginOk, body, error)) {
            Disconnect();
            return false;
        }
        const auto pairs = ParsePairs(body);
        session_ = static_cast<std::uint32_t>(std::strtoul(pairs.at("session").c_str(), nullptr, 10));
        home_ = pairs.at("home");
        user_ = pairs.at("username");
        admin_ = pairs.at("admin") == "1";
        return true;
    }

    void Disconnect() {
        if (sock_ != INVALID_SOCKET && session_ != 0) {
            SendPacket(sock_, Cmd::Logout, nextSeq_++, session_, "");
        }
        CloseSocket(sock_);
        session_ = 0;
        admin_ = false;
        home_.clear();
        user_.clear();
    }

    bool Connected() const { return sock_ != INVALID_SOCKET && session_ != 0; }
    std::uint32_t Session() const { return session_; }
    const std::string& Host() const { return host_; }
    int Port() const { return port_; }
    const std::string& Home() const { return home_; }
    const std::string& Username() const { return user_; }

    bool List(const std::string& path, std::string& cwd, std::vector<FileEntry>& items, std::string& error) {
        std::string body;
        if (!Call(Cmd::List, {{"path", path}}, Cmd::ListResult, body, error)) {
            return false;
        }
        items.clear();
        for (const auto& row : ParseLines(body)) {
            if (row.empty()) {
                continue;
            }
            if (row[0] == "PWD" && row.size() >= 2) {
                cwd = row[1];
                continue;
            }
            if (row[0] == "E" && row.size() >= 6) {
                FileEntry entry;
                entry.name = row[1];
                entry.path = row[2];
                entry.isDir = row[3] == "1";
                entry.size = std::strtoull(row[4].c_str(), nullptr, 10);
                entry.mtime = row[5];
                items.push_back(std::move(entry));
            }
        }
        return true;
    }

    bool MakeDir(const std::string& path, std::string& error) {
        std::string body;
        return Call(Cmd::MakeDir, {{"path", path}}, Cmd::Ok, body, error);
    }

    bool Remove(const std::string& path, std::string& error) {
        std::string body;
        return Call(Cmd::Remove, {{"path", path}}, Cmd::Ok, body, error);
    }

    bool Rename(const std::string& path, const std::string& newName, std::string& error) {
        std::string body;
        return Call(Cmd::Rename, {{"path", path}, {"new_name", newName}}, Cmd::Ok, body, error);
    }

private:
    SOCKET sock_ = INVALID_SOCKET;
    std::uint32_t session_ = 0;
    std::uint32_t nextSeq_ = 1;
    std::string host_;
    std::string home_;
    std::string user_;
    int port_ = kDefaultPort;
    bool admin_ = false;

    bool Call(Cmd cmd, const std::map<std::string, std::string>& req, Cmd expected, std::string& body, std::string& error) {
        if (sock_ == INVALID_SOCKET) {
            error = "尚未连接";
            return false;
        }
        if (!SendPacket(sock_, cmd, nextSeq_++, session_, SerializePairs(req))) {
            error = "请求发送失败";
            return false;
        }
        NetPacket packet;
        if (!RecvPacket(sock_, packet)) {
            error = "服务器无响应";
            return false;
        }
        if (packet.cmd == Cmd::Error) {
            error = ParsePairs(packet.body)["message"];
            return false;
        }
        if (packet.cmd != expected) {
            error = "服务器返回了未知响应";
            return false;
        }
        body = packet.body;
        return true;
    }
};

bool UploadFileSync(const ConnectionInfo& conn, const std::filesystem::path& localPath, const std::string& remotePath,
                    std::string& error, std::function<FlowControl(std::uint64_t, std::uint64_t)> onProgress) {
    std::error_code ec;
    const auto total = std::filesystem::file_size(localPath, ec);
    if (ec) {
        error = "本地文件不可读";
        return false;
    }
    const auto localHash = Sha256File(localPath);
    SOCKET sock = ConnectSocket(conn.host, conn.port);
    if (sock == INVALID_SOCKET) {
        error = "上传连接失败";
        return false;
    }
    if (!SendPacket(sock, Cmd::UploadBegin, 1, conn.session, SerializePairs({
            {"path", remotePath},
            {"size", std::to_string(total)},
            {"sha256", localHash},
        }))) {
        CloseSocket(sock);
        error = "发送上传请求失败";
        return false;
    }
    NetPacket packet;
    if (!RecvPacket(sock, packet)) {
        CloseSocket(sock);
        error = "服务器未响应上传请求";
        return false;
    }
    if (packet.cmd == Cmd::Error) {
        CloseSocket(sock);
        error = ParsePairs(packet.body)["message"];
        return false;
    }
    const auto meta = ParsePairs(packet.body);
    std::uint64_t offset = std::strtoull(meta.at("offset").c_str(), nullptr, 10);
    if (offset > total) {
        CloseSocket(sock);
        error = "服务器偏移量异常";
        return false;
    }
    std::ifstream in(localPath, std::ios::binary);
    in.seekg(static_cast<std::streamoff>(offset));
    std::vector<char> buffer(kChunkSize);
    std::uint64_t done = offset;
    while (in) {
        if (auto ctrl = onProgress(done, total); ctrl != FlowControl::Continue) {
            CloseSocket(sock);
            error = ctrl == FlowControl::Pause ? "__PAUSE__" : "__CANCEL__";
            return false;
        }
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = in.gcount();
        if (got <= 0) {
            break;
        }
        if (!SendPacket(sock, Cmd::UploadData, 1, conn.session, std::string(buffer.data(), static_cast<std::size_t>(got)))) {
            CloseSocket(sock);
            error = "上传数据发送失败";
            return false;
        }
        done += static_cast<std::uint64_t>(got);
    }
    if (!SendPacket(sock, Cmd::UploadEnd, 1, conn.session, SerializePairs({{"sha256", localHash}}))) {
        CloseSocket(sock);
        error = "上传结束通知失败";
        return false;
    }
    if (!RecvPacket(sock, packet)) {
        CloseSocket(sock);
        error = "服务器未返回校验结果";
        return false;
    }
    CloseSocket(sock);
    if (packet.cmd == Cmd::Error) {
        error = ParsePairs(packet.body)["message"];
        return false;
    }
    const auto result = ParsePairs(packet.body);
    if (result.at("sha256") != localHash) {
        error = "上传后哈希不一致";
        return false;
    }
    return true;
}

bool DownloadFileSync(const ConnectionInfo& conn, const std::string& remotePath, const std::filesystem::path& localPath,
                      std::string& error, std::function<FlowControl(std::uint64_t, std::uint64_t)> onProgress) {
    if (!localPath.parent_path().empty()) {
        std::filesystem::create_directories(localPath.parent_path());
    }
    std::error_code ec;
    std::uint64_t offset = std::filesystem::exists(localPath, ec) ? std::filesystem::file_size(localPath, ec) : 0;
    SOCKET sock = ConnectSocket(conn.host, conn.port);
    if (sock == INVALID_SOCKET) {
        error = "下载连接失败";
        return false;
    }
    if (!SendPacket(sock, Cmd::DownloadBegin, 1, conn.session, SerializePairs({
            {"path", remotePath},
            {"offset", std::to_string(offset)},
        }))) {
        CloseSocket(sock);
        error = "发送下载请求失败";
        return false;
    }
    NetPacket packet;
    if (!RecvPacket(sock, packet)) {
        CloseSocket(sock);
        error = "服务器未响应下载请求";
        return false;
    }
    if (packet.cmd == Cmd::Error) {
        CloseSocket(sock);
        error = ParsePairs(packet.body)["message"];
        return false;
    }
    const auto meta = ParsePairs(packet.body);
    const std::uint64_t total = std::strtoull(meta.at("size").c_str(), nullptr, 10);
    offset = std::strtoull(meta.at("offset").c_str(), nullptr, 10);
    std::ofstream out;
    if (offset == 0) {
        out.open(localPath, std::ios::binary | std::ios::trunc);
    } else {
        out.open(localPath, std::ios::binary | std::ios::app);
    }
    if (!out) {
        CloseSocket(sock);
        error = "本地文件无法写入";
        return false;
    }

    std::uint64_t done = offset;
    while (true) {
        if (!RecvPacket(sock, packet)) {
            CloseSocket(sock);
            error = "下载连接中断";
            return false;
        }
        if (packet.cmd == Cmd::DownloadData) {
            out.write(packet.body.data(), static_cast<std::streamsize>(packet.body.size()));
            done += static_cast<std::uint64_t>(packet.body.size());
            if (auto ctrl = onProgress(done, total); ctrl != FlowControl::Continue) {
                CloseSocket(sock);
                error = ctrl == FlowControl::Pause ? "__PAUSE__" : "__CANCEL__";
                return false;
            }
            continue;
        }
        if (packet.cmd == Cmd::DownloadDone) {
            out.flush();
            CloseSocket(sock);
            const auto hash = ParsePairs(packet.body).at("sha256");
            if (Sha256File(localPath) != hash) {
                error = "下载后哈希不一致";
                return false;
            }
            return true;
        }
        if (packet.cmd == Cmd::Error) {
            CloseSocket(sock);
            error = ParsePairs(packet.body)["message"];
            return false;
        }
    }
}

struct Favorite {
    std::string host;
    int port = kDefaultPort;
    std::string user;
    bool anonymous = false;
};

enum class TaskState { Queued, Running, Paused, Completed, Failed, Cancelled };

struct Task {
    int id = 0;
    bool upload = false;
    std::wstring local;
    std::string remote;
    std::atomic<std::uint64_t> done{0};
    std::atomic<std::uint64_t> total{0};
    std::atomic<double> speed{0.0};
    std::atomic<TaskState> state{TaskState::Queued};
    std::atomic<bool> pauseWanted{false};
    std::atomic<bool> cancelWanted{false};
    std::mutex infoMu;
    std::wstring info = L"等待";
};

std::wstring TaskStateText(TaskState state) {
    switch (state) {
        case TaskState::Queued: return L"等待";
        case TaskState::Running: return L"传输中";
        case TaskState::Paused: return L"已暂停";
        case TaskState::Completed: return L"已完成";
        case TaskState::Failed: return L"失败";
        case TaskState::Cancelled: return L"已取消";
    }
    return L"未知";
}

class ClientWindow {
public:
    explicit ClientWindow(HINSTANCE instance)
        : instance_(instance),
          favoritesFile_(std::filesystem::current_path() / L"data" / L"client_favorites.tsv") {}

    int Run(int showCmd) {
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&icc);

        WNDCLASSW wc{};
        wc.lpfnWndProc = &ClientWindow::WndProc;
        wc.hInstance = instance_;
        wc.lpszClassName = L"FdsClientWindow";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);

        hwnd_ = CreateWindowW(wc.lpszClassName, L"FDS 客户端",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760,
                              nullptr, nullptr, instance_, this);
        ShowWindow(hwnd_, showCmd);
        UpdateWindow(hwnd_);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

private:
    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND hostEdit_{};
    HWND portEdit_{};
    HWND userEdit_{};
    HWND passEdit_{};
    HWND anonCheck_{};
    HWND favoritesBox_{};
    HWND pathEdit_{};
    HWND fileList_{};
    HWND inputEdit_{};
    HWND taskList_{};
    HWND status_{};

    std::filesystem::path favoritesFile_;
    CommandClient client_;
    std::vector<Favorite> favorites_;
    std::vector<FileEntry> entries_;
    std::vector<std::shared_ptr<Task>> tasks_;
    std::vector<std::string> history_;
    int historyIndex_ = -1;
    int nextTaskId_ = 1;

    void BuildUi() {
        CreateWindowW(L"STATIC", L"服务器", WS_CHILD | WS_VISIBLE, 16, 20, 50, 24, hwnd_, nullptr, instance_, nullptr);
        hostEdit_ = CreateWindowW(L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER, 68, 18, 120, 24, hwnd_, reinterpret_cast<HMENU>(IDC_HOST), instance_, nullptr);
        CreateWindowW(L"STATIC", L"端口", WS_CHILD | WS_VISIBLE, 196, 20, 40, 24, hwnd_, nullptr, instance_, nullptr);
        portEdit_ = CreateWindowW(L"EDIT", L"9527", WS_CHILD | WS_VISIBLE | WS_BORDER, 236, 18, 70, 24, hwnd_, reinterpret_cast<HMENU>(IDC_PORT), instance_, nullptr);
        CreateWindowW(L"STATIC", L"用户名", WS_CHILD | WS_VISIBLE, 314, 20, 50, 24, hwnd_, nullptr, instance_, nullptr);
        userEdit_ = CreateWindowW(L"EDIT", L"demo", WS_CHILD | WS_VISIBLE | WS_BORDER, 368, 18, 110, 24, hwnd_, reinterpret_cast<HMENU>(IDC_USER), instance_, nullptr);
        CreateWindowW(L"STATIC", L"密码", WS_CHILD | WS_VISIBLE, 486, 20, 40, 24, hwnd_, nullptr, instance_, nullptr);
        passEdit_ = CreateWindowW(L"EDIT", L"demo123", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD, 528, 18, 110, 24, hwnd_, reinterpret_cast<HMENU>(IDC_PASS), instance_, nullptr);
        anonCheck_ = CreateWindowW(L"BUTTON", L"匿名", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 648, 18, 60, 24, hwnd_, reinterpret_cast<HMENU>(IDC_ANON), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"连接", WS_CHILD | WS_VISIBLE, 720, 16, 70, 28, hwnd_, reinterpret_cast<HMENU>(IDC_CONNECT), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"保存连接", WS_CHILD | WS_VISIBLE, 798, 16, 90, 28, hwnd_, reinterpret_cast<HMENU>(IDC_SAVE_FAV), instance_, nullptr);
        favoritesBox_ = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST, 896, 18, 180, 300, hwnd_, reinterpret_cast<HMENU>(IDC_FAVORITES), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"载入", WS_CHILD | WS_VISIBLE, 1084, 16, 70, 28, hwnd_, reinterpret_cast<HMENU>(IDC_LOAD_FAV), instance_, nullptr);

        CreateWindowW(L"BUTTON", L"<", WS_CHILD | WS_VISIBLE, 16, 56, 32, 28, hwnd_, reinterpret_cast<HMENU>(IDC_BACK), instance_, nullptr);
        CreateWindowW(L"BUTTON", L">", WS_CHILD | WS_VISIBLE, 52, 56, 32, 28, hwnd_, reinterpret_cast<HMENU>(IDC_FORWARD), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"上级", WS_CHILD | WS_VISIBLE, 88, 56, 56, 28, hwnd_, reinterpret_cast<HMENU>(IDC_UP), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE, 148, 56, 56, 28, hwnd_, reinterpret_cast<HMENU>(IDC_REFRESH), instance_, nullptr);
        pathEdit_ = CreateWindowW(L"EDIT", L"/", WS_CHILD | WS_VISIBLE | WS_BORDER, 214, 58, 540, 24, hwnd_, reinterpret_cast<HMENU>(IDC_PATH), instance_, nullptr);
        CreateWindowW(L"STATIC", L"输入名称", WS_CHILD | WS_VISIBLE, 770, 60, 60, 24, hwnd_, nullptr, instance_, nullptr);
        inputEdit_ = CreateWindowW(L"EDIT", L"new_folder", WS_CHILD | WS_VISIBLE | WS_BORDER, 834, 58, 200, 24, hwnd_, reinterpret_cast<HMENU>(IDC_INPUT), instance_, nullptr);

        fileList_ = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                  16, 96, 740, 360, hwnd_, reinterpret_cast<HMENU>(IDC_FILES), instance_, nullptr);
        ListView_SetExtendedListViewStyle(fileList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddColumn(fileList_, 0, 220, L"名称");
        AddColumn(fileList_, 1, 80, L"类型");
        AddColumn(fileList_, 2, 120, L"大小");
        AddColumn(fileList_, 3, 180, L"修改时间");
        AddColumn(fileList_, 4, 220, L"路径");

        CreateWindowW(L"BUTTON", L"上传文件", WS_CHILD | WS_VISIBLE, 780, 110, 110, 30, hwnd_, reinterpret_cast<HMENU>(IDC_UPLOAD_FILE), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"上传目录", WS_CHILD | WS_VISIBLE, 900, 110, 110, 30, hwnd_, reinterpret_cast<HMENU>(IDC_UPLOAD_DIR), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"下载", WS_CHILD | WS_VISIBLE, 1020, 110, 110, 30, hwnd_, reinterpret_cast<HMENU>(IDC_DOWNLOAD), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE, 780, 150, 110, 30, hwnd_, reinterpret_cast<HMENU>(IDC_DELETE), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"新建目录", WS_CHILD | WS_VISIBLE, 900, 150, 110, 30, hwnd_, reinterpret_cast<HMENU>(IDC_MKDIR), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"重命名", WS_CHILD | WS_VISIBLE, 1020, 150, 110, 30, hwnd_, reinterpret_cast<HMENU>(IDC_RENAME), instance_, nullptr);
        CreateWindowW(L"STATIC", L"说明: 上传/下载任务支持暂停、继续、取消，暂停后可直接继续断点续传。", WS_CHILD | WS_VISIBLE,
                      780, 200, 340, 50, hwnd_, nullptr, instance_, nullptr);

        taskList_ = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                  16, 474, 1114, 190, hwnd_, reinterpret_cast<HMENU>(IDC_TASKS), instance_, nullptr);
        ListView_SetExtendedListViewStyle(taskList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddColumn(taskList_, 0, 50, L"ID");
        AddColumn(taskList_, 1, 70, L"类型");
        AddColumn(taskList_, 2, 260, L"本地");
        AddColumn(taskList_, 3, 260, L"远程");
        AddColumn(taskList_, 4, 110, L"进度");
        AddColumn(taskList_, 5, 90, L"速度");
        AddColumn(taskList_, 6, 90, L"状态");
        AddColumn(taskList_, 7, 170, L"说明");

        CreateWindowW(L"BUTTON", L"暂停", WS_CHILD | WS_VISIBLE, 780, 676, 90, 28, hwnd_, reinterpret_cast<HMENU>(IDC_PAUSE), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"继续", WS_CHILD | WS_VISIBLE, 878, 676, 90, 28, hwnd_, reinterpret_cast<HMENU>(IDC_RESUME), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE, 976, 676, 90, 28, hwnd_, reinterpret_cast<HMENU>(IDC_CANCEL), instance_, nullptr);
        status_ = CreateWindowW(L"STATIC", L"未连接", WS_CHILD | WS_VISIBLE, 16, 680, 740, 24, hwnd_, reinterpret_cast<HMENU>(IDC_STATUS), instance_, nullptr);

        LoadFavorites();
        SetTimer(hwnd_, 1, 500, nullptr);
    }

    void SetStatus(const std::wstring& text) {
        SetText(status_, text);
    }

    void LoadFavorites() {
        favorites_.clear();
        SendMessageW(favoritesBox_, CB_RESETCONTENT, 0, 0);
        for (const auto& row : ParseLines(ReadTextFile())) {
            if (row.size() < 4) {
                continue;
            }
            Favorite fav;
            fav.host = row[0];
            fav.port = std::atoi(row[1].c_str());
            fav.user = row[2];
            fav.anonymous = row[3] == "1";
            favorites_.push_back(fav);
        }
        for (const auto& fav : favorites_) {
            const auto label = Utf8ToWide(fav.host + ":" + std::to_string(fav.port) + " / " + fav.user + (fav.anonymous ? " (anon)" : ""));
            SendMessageW(favoritesBox_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (!favorites_.empty()) {
            SendMessageW(favoritesBox_, CB_SETCURSEL, 0, 0);
        }
    }

    std::string ReadTextFile() const {
        std::ifstream in(favoritesFile_);
        if (!in) {
            return "";
        }
        std::ostringstream oss;
        oss << in.rdbuf();
        return oss.str();
    }

    void SaveFavoritesFile() {
        std::filesystem::create_directories(favoritesFile_.parent_path());
        std::ofstream out(favoritesFile_, std::ios::trunc);
        for (const auto& fav : favorites_) {
            out << MakeLine({fav.host, std::to_string(fav.port), fav.user, fav.anonymous ? "1" : "0"});
        }
    }

    void SaveFavorite() {
        Favorite fav;
        fav.host = WideToUtf8(GetText(hostEdit_));
        fav.port = _wtoi(GetText(portEdit_).c_str());
        fav.user = WideToUtf8(GetText(userEdit_));
        fav.anonymous = Button_GetCheck(anonCheck_) == BST_CHECKED;
        for (const auto& item : favorites_) {
            if (item.host == fav.host && item.port == fav.port && item.user == fav.user && item.anonymous == fav.anonymous) {
                return;
            }
        }
        favorites_.push_back(fav);
        SaveFavoritesFile();
        LoadFavorites();
    }

    void LoadFavoriteSelection() {
        const int index = static_cast<int>(SendMessageW(favoritesBox_, CB_GETCURSEL, 0, 0));
        if (index < 0 || index >= static_cast<int>(favorites_.size())) {
            return;
        }
        const auto& fav = favorites_[index];
        SetText(hostEdit_, Utf8ToWide(fav.host));
        SetText(portEdit_, Utf8ToWide(std::to_string(fav.port)));
        SetText(userEdit_, Utf8ToWide(fav.user));
        Button_SetCheck(anonCheck_, fav.anonymous ? BST_CHECKED : BST_UNCHECKED);
    }

    void ConnectServer() {
        if (client_.Connected()) {
            client_.Disconnect();
            SetStatus(L"连接已断开");
            return;
        }
        std::string error;
        const bool anonymous = Button_GetCheck(anonCheck_) == BST_CHECKED;
        if (!client_.Connect(WideToUtf8(GetText(hostEdit_)), _wtoi(GetText(portEdit_).c_str()),
                             WideToUtf8(GetText(userEdit_)), WideToUtf8(GetText(passEdit_)),
                             anonymous, error)) {
            Alert(hwnd_, Utf8ToWide(error));
            return;
        }
        SetStatus(L"连接成功");
        SetWindowTextW(GetDlgItem(hwnd_, IDC_CONNECT), L"断开");
        Browse(client_.Home(), true);
    }

    void Browse(const std::string& path, bool pushHistory) {
        if (!client_.Connected()) {
            return;
        }
        std::string error;
        std::string cwd;
        if (!client_.List(path, cwd, entries_, error)) {
            Alert(hwnd_, Utf8ToWide(error));
            return;
        }
        SetText(pathEdit_, Utf8ToWide(cwd));
        if (pushHistory) {
            if (historyIndex_ + 1 < static_cast<int>(history_.size())) {
                history_.erase(history_.begin() + historyIndex_ + 1, history_.end());
            }
            history_.push_back(cwd);
            historyIndex_ = static_cast<int>(history_.size()) - 1;
        }
        RefreshFileList();
    }

    void RefreshFileList() {
        ListView_DeleteAllItems(fileList_);
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            const auto& item = entries_[i];
            LVITEMW row{};
            row.mask = LVIF_TEXT;
            row.iItem = i;
            auto name = Utf8ToWide(item.name);
            row.pszText = name.data();
            ListView_InsertItem(fileList_, &row);
            ListView_SetItemText(fileList_, i, 1, const_cast<wchar_t*>(item.isDir ? L"目录" : L"文件"));
            auto size = Utf8ToWide(item.isDir ? "-" : FormatBytes(item.size));
            auto time = Utf8ToWide(item.mtime);
            auto path = Utf8ToWide(item.path);
            ListView_SetItemText(fileList_, i, 2, size.data());
            ListView_SetItemText(fileList_, i, 3, time.data());
            ListView_SetItemText(fileList_, i, 4, path.data());
        }
    }

    std::optional<FileEntry> SelectedEntry() const {
        const int index = ListView_GetNextItem(fileList_, -1, LVNI_SELECTED);
        if (index < 0 || index >= static_cast<int>(entries_.size())) {
            return std::nullopt;
        }
        return entries_[index];
    }

    std::shared_ptr<Task> SelectedTask() const {
        const int index = ListView_GetNextItem(taskList_, -1, LVNI_SELECTED);
        if (index < 0 || index >= static_cast<int>(tasks_.size())) {
            return nullptr;
        }
        return tasks_[index];
    }

    ConnectionInfo Conn() const {
        return {client_.Host(), client_.Port(), client_.Session()};
    }

    void AddTask(bool upload, const std::wstring& local, const std::string& remote) {
        auto task = std::make_shared<Task>();
        task->id = nextTaskId_++;
        task->upload = upload;
        task->local = local;
        task->remote = remote;
        tasks_.push_back(task);
        StartTask(task);
        RefreshTasks();
    }

    void StartTask(const std::shared_ptr<Task>& task) {
        task->state = TaskState::Running;
        task->pauseWanted = false;
        task->cancelWanted = false;
        std::thread([this, task] {
            std::string error;
            const auto started = std::chrono::steady_clock::now();
            std::uint64_t lastDone = task->done.load();
            auto lastTick = started;
            auto progress = [&](std::uint64_t done, std::uint64_t total) {
                task->done = done;
                task->total = total;
                const auto now = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration<double>(now - lastTick).count();
                if (elapsed >= 0.3) {
                    task->speed = (done - lastDone) / elapsed;
                    lastDone = done;
                    lastTick = now;
                }
                if (task->cancelWanted) return FlowControl::Cancel;
                // 暂停的实现是主动断开传输连接，之后再按偏移量重连续传。
                if (task->pauseWanted) return FlowControl::Pause;
                return FlowControl::Continue;
            };

            bool ok = false;
            if (task->upload) {
                ok = UploadFileSync(Conn(), task->local, task->remote, error, progress);
            } else {
                ok = DownloadFileSync(Conn(), task->remote, task->local, error, progress);
            }

            std::lock_guard lock(task->infoMu);
            if (ok) {
                task->state = TaskState::Completed;
                task->info = L"哈希校验通过";
                task->speed = 0;
                return;
            }
            if (error == "__PAUSE__") {
                task->state = TaskState::Paused;
                task->info = L"已暂停，可继续";
                task->speed = 0;
                return;
            }
            if (error == "__CANCEL__") {
                task->state = TaskState::Cancelled;
                task->info = L"已取消";
                task->speed = 0;
                return;
            }
            task->state = TaskState::Failed;
            task->info = Utf8ToWide(error);
            task->speed = 0;
        }).detach();
    }

    void RefreshTasks() {
        ListView_DeleteAllItems(taskList_);
        for (int i = 0; i < static_cast<int>(tasks_.size()); ++i) {
            const auto& task = tasks_[i];
            LVITEMW row{};
            row.mask = LVIF_TEXT;
            row.iItem = i;
            auto id = Utf8ToWide(std::to_string(task->id));
            row.pszText = id.data();
            ListView_InsertItem(taskList_, &row);

            const auto type = task->upload ? L"上传" : L"下载";
            auto local = task->local;
            auto remote = Utf8ToWide(task->remote);
            std::wstring progress = Utf8ToWide(FormatBytes(task->done)) + L" / " + Utf8ToWide(FormatBytes(task->total));
            std::wstring speed = Utf8ToWide(FormatBytes(static_cast<std::uint64_t>(task->speed.load()))) + L"/s";
            auto state = TaskStateText(task->state.load());
            std::wstring info;
            {
                std::lock_guard lock(task->infoMu);
                info = task->info;
            }

            ListView_SetItemText(taskList_, i, 1, const_cast<wchar_t*>(type));
            ListView_SetItemText(taskList_, i, 2, local.data());
            ListView_SetItemText(taskList_, i, 3, remote.data());
            ListView_SetItemText(taskList_, i, 4, progress.data());
            ListView_SetItemText(taskList_, i, 5, speed.data());
            ListView_SetItemText(taskList_, i, 6, state.data());
            ListView_SetItemText(taskList_, i, 7, info.data());
        }
    }

    void UploadFile() {
        if (!client_.Connected()) return;
        const auto file = PickOpenFile(hwnd_);
        if (file.empty()) return;
        const auto name = WideToUtf8(std::filesystem::path(file).filename().wstring());
        AddTask(true, file, JoinRemote(WideToUtf8(GetText(pathEdit_)), name));
    }

    void UploadDir() {
        if (!client_.Connected()) return;
        const auto dir = PickFolder(hwnd_);
        if (dir.empty()) return;
        const auto baseName = WideToUtf8(std::filesystem::path(dir).filename().wstring());
        const auto remoteBase = JoinRemote(WideToUtf8(GetText(pathEdit_)), baseName);
        std::string error;
        client_.MakeDir(remoteBase, error);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            const auto rel = entry.path().lexically_relative(dir);
            const auto relUtf8 = WideToUtf8(rel.generic_wstring());
            const auto remote = JoinRemote(remoteBase, relUtf8);
            if (entry.is_directory()) {
                client_.MakeDir(remote, error);
            } else if (entry.is_regular_file()) {
                AddTask(true, entry.path().wstring(), remote);
            }
        }
        Browse(WideToUtf8(GetText(pathEdit_)), false);
    }

    void DownloadFile() {
        if (!client_.Connected()) return;
        const auto selected = SelectedEntry();
        if (!selected || selected->isDir) {
            Alert(hwnd_, L"请选择文件下载");
            return;
        }
        const auto local = PickSaveFile(hwnd_, Utf8ToWide(selected->name));
        if (local.empty()) return;
        AddTask(false, local, selected->path);
    }

    void RemoveEntry() {
        if (!client_.Connected()) return;
        const auto selected = SelectedEntry();
        if (!selected) return;
        std::string error;
        if (!client_.Remove(selected->path, error)) {
            Alert(hwnd_, Utf8ToWide(error));
            return;
        }
        Browse(WideToUtf8(GetText(pathEdit_)), false);
    }

    void MakeDir() {
        if (!client_.Connected()) return;
        const auto name = WideToUtf8(GetText(inputEdit_));
        std::string error;
        if (!client_.MakeDir(JoinRemote(WideToUtf8(GetText(pathEdit_)), name), error)) {
            Alert(hwnd_, Utf8ToWide(error));
            return;
        }
        Browse(WideToUtf8(GetText(pathEdit_)), false);
    }

    void RenameEntry() {
        if (!client_.Connected()) return;
        const auto selected = SelectedEntry();
        if (!selected) return;
        std::string error;
        if (!client_.Rename(selected->path, WideToUtf8(GetText(inputEdit_)), error)) {
            Alert(hwnd_, Utf8ToWide(error));
            return;
        }
        Browse(WideToUtf8(GetText(pathEdit_)), false);
    }

    void PauseTask() {
        if (const auto task = SelectedTask()) {
            task->pauseWanted = true;
        }
    }

    void ResumeTask() {
        if (const auto task = SelectedTask()) {
            if (task->state == TaskState::Paused || task->state == TaskState::Failed) {
                StartTask(task);
            }
        }
    }

    void CancelTask() {
        if (const auto task = SelectedTask()) {
            task->cancelWanted = true;
        }
    }

    void Back() {
        if (historyIndex_ > 0) {
            --historyIndex_;
            Browse(history_[historyIndex_], false);
        }
    }

    void Forward() {
        if (historyIndex_ + 1 < static_cast<int>(history_.size())) {
            ++historyIndex_;
            Browse(history_[historyIndex_], false);
        }
    }

    void Up() {
        const auto current = NormalizeVirtualPath(WideToUtf8(GetText(pathEdit_)));
        if (current == "/" || current.empty()) {
            return;
        }
        const auto pos = current.find_last_of('/');
        const auto parent = pos <= 0 ? "/" : current.substr(0, pos);
        Browse(parent, true);
    }

    void EnterSelected() {
        const auto selected = SelectedEntry();
        if (!selected) return;
        if (selected->isDir) {
            Browse(selected->path, true);
        }
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
                self->RefreshTasks();
                return 0;
            case WM_NOTIFY:
                if (reinterpret_cast<LPNMHDR>(lParam)->idFrom == IDC_FILES &&
                    reinterpret_cast<LPNMHDR>(lParam)->code == NM_DBLCLK) {
                    self->EnterSelected();
                }
                return 0;
            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case IDC_CONNECT: self->ConnectServer(); return 0;
                    case IDC_SAVE_FAV: self->SaveFavorite(); return 0;
                    case IDC_LOAD_FAV: self->LoadFavoriteSelection(); return 0;
                    case IDC_REFRESH: self->Browse(WideToUtf8(GetText(self->pathEdit_)), false); return 0;
                    case IDC_BACK: self->Back(); return 0;
                    case IDC_FORWARD: self->Forward(); return 0;
                    case IDC_UP: self->Up(); return 0;
                    case IDC_UPLOAD_FILE: self->UploadFile(); return 0;
                    case IDC_UPLOAD_DIR: self->UploadDir(); return 0;
                    case IDC_DOWNLOAD: self->DownloadFile(); return 0;
                    case IDC_DELETE: self->RemoveEntry(); return 0;
                    case IDC_MKDIR: self->MakeDir(); return 0;
                    case IDC_RENAME: self->RenameEntry(); return 0;
                    case IDC_PAUSE: self->PauseTask(); return 0;
                    case IDC_RESUME: self->ResumeTask(); return 0;
                    case IDC_CANCEL: self->CancelTask(); return 0;
                    default: break;
                }
                break;
            case WM_DESTROY:
                KillTimer(hwnd, 1);
                self->client_.Disconnect();
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

int RunScript(int argc, wchar_t** argv) {
    std::string host = "127.0.0.1";
    int port = kDefaultPort;
    std::string user = "demo";
    std::string pass = "demo123";
    bool anonymous = false;
    std::string listPath = "/";
    std::string uploadLocal;
    std::string uploadRemote;
    std::string downloadRemote;
    std::string downloadLocal;

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv[i];
        auto next = [&](std::string& out) {
            if (i + 1 < argc) out = WideToUtf8(argv[++i]);
        };
        if (arg == L"--host") next(host);
        else if (arg == L"--port" && i + 1 < argc) port = _wtoi(argv[++i]);
        else if (arg == L"--user") next(user);
        else if (arg == L"--pass") next(pass);
        else if (arg == L"--anonymous") anonymous = true;
        else if (arg == L"--list") next(listPath);
        else if (arg == L"--upload-local") next(uploadLocal);
        else if (arg == L"--upload-remote") next(uploadRemote);
        else if (arg == L"--download-remote") next(downloadRemote);
        else if (arg == L"--download-local") next(downloadLocal);
    }

    CommandClient client;
    std::string error;
    if (!client.Connect(host, port, user, pass, anonymous, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    if (!uploadLocal.empty() && !uploadRemote.empty()) {
        if (!UploadFileSync({host, port, client.Session()}, Utf8ToWide(uploadLocal), uploadRemote, error,
                            [](std::uint64_t, std::uint64_t) { return FlowControl::Continue; })) {
            std::cerr << error << "\n";
            return 1;
        }
        std::cout << "upload ok\n";
    }
    if (!downloadRemote.empty() && !downloadLocal.empty()) {
        if (!DownloadFileSync({host, port, client.Session()}, downloadRemote, Utf8ToWide(downloadLocal), error,
                              [](std::uint64_t, std::uint64_t) { return FlowControl::Continue; })) {
            std::cerr << error << "\n";
            return 1;
        }
        std::cout << "download ok\n";
    }
    std::string cwd;
    std::vector<FileEntry> items;
    if (!client.List(listPath, cwd, items, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << "cwd: " << cwd << "\n";
    for (const auto& item : items) {
        std::cout << (item.isDir ? "[D] " : "[F] ") << item.path << "  " << item.mtime << "  " << item.size << "\n";
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    ScopedSockets sockets;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring_view(argv[i]) == L"--script") {
            return RunScript(argc, argv);
        }
    }
    ClientWindow app(GetModuleHandleW(nullptr));
    return app.Run(SW_SHOW);
}
