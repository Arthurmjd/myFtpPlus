#include "common.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <deque>
#include <iostream>
#include <unordered_map>

using namespace fds;

namespace {

constexpr int IDC_PORT = 1001;
constexpr int IDC_START = 1002;
constexpr int IDC_STATUS = 1003;
constexpr int IDC_ADMIN_PASS = 1004;
constexpr int IDC_ADMIN_LOGIN = 1005;
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
    MessageBoxW(hwnd, text.c_str(), L"FDS", MB_OK | MB_ICONINFORMATION);
}

std::string ReadFileUtf8(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return "";
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

UserRecord MakeDefaultUser(const std::string& name, const std::string& plainPassword, bool admin = false) {
    UserRecord user;
    user.username = name;
    user.passwordHash = Sha256String(plainPassword);
    user.enabled = true;
    user.admin = admin;
    if (admin) {
        user.home = "/";
        user.ruleSpec = "/:RWDN";
    } else {
        user.home = "/users/" + name;
        user.ruleSpec = "/public:R;/download:R;/users/" + name + ":RWDN;/upload/" + name + ":RWDN";
    }
    user.rules = ParseRuleSpec(user.ruleSpec);
    return user;
}

std::vector<PermissionRule> AnonymousRules() {
    return ParseRuleSpec("/public:R;/download:R");
}

class ServerCore {
public:
    ServerCore()
        : dataDir_(std::filesystem::current_path() / L"data"),
          rootDir_(dataDir_ / L"server_root"),
          usersFile_(dataDir_ / L"users.tsv"),
          logFile_(dataDir_ / L"server.log") {
        EnsureLayout();
    }

    ~ServerCore() {
        Stop();
    }

    bool Start(int port, std::string& error) {
        if (running_) {
            return true;
        }
        SOCKET listenSock = CreateListenSocket(port);
        if (listenSock == INVALID_SOCKET) {
            error = "端口监听失败";
            return false;
        }
        port_ = port;
        listenSock_ = listenSock;
        running_ = true;
        loopThread_ = std::thread([this] { Loop(); });
        AppendLog("system", "server_start", "listen " + std::to_string(port_));
        return true;
    }

    void Stop() {
        if (!running_) {
            return;
        }
        running_ = false;
        CloseSocket(listenSock_);
        if (loopThread_.joinable()) {
            loopThread_.join();
        }
        std::lock_guard lock(mu_);
        for (auto sock : clientSockets_) {
            auto tmp = sock;
            CloseSocket(tmp);
        }
        clientSockets_.clear();
        sessions_.clear();
        activeClients_ = 0;
        AppendLog("system", "server_stop", "stopped");
    }

    bool IsRunning() const { return running_; }

    std::wstring StatusText() const {
        std::ostringstream oss;
        oss << "端口: " << port_
            << "  连接: " << activeClients_.load()
            << "  传输线程: " << activeTransfers_.load()
            << "  服务: " << (running_ ? "运行中" : "已停止");
        return Utf8ToWide(oss.str());
    }

    bool VerifyAdmin(const std::string& password) {
        std::lock_guard lock(mu_);
        const auto it = users_.find("admin");
        return it != users_.end() && it->second.enabled && it->second.passwordHash == Sha256String(password);
    }

    std::vector<UserRecord> SnapshotUsers() {
        std::lock_guard lock(mu_);
        std::vector<UserRecord> out;
        for (const auto& [_, user] : users_) {
            out.push_back(user);
        }
        std::sort(out.begin(), out.end(), [](const UserRecord& a, const UserRecord& b) {
            return _stricmp(a.username.c_str(), b.username.c_str()) < 0;
        });
        return out;
    }

    bool UpsertUser(const UserRecord& input, const std::string& plainPassword, std::string& error) {
        if (input.username.empty()) {
            error = "用户名不能为空";
            return false;
        }
        if (input.home.empty() || NormalizeVirtualPath(input.home).empty()) {
            error = "主目录无效";
            return false;
        }
        auto user = input;
        user.home = NormalizeVirtualPath(user.home);
        user.ruleSpec = Trim(user.ruleSpec);
        user.rules = ParseRuleSpec(user.ruleSpec);
        if (user.rules.empty()) {
            error = "权限规则不能为空";
            return false;
        }

        std::lock_guard lock(mu_);
        auto it = users_.find(user.username);
        if (it != users_.end()) {
            if (!plainPassword.empty()) {
                user.passwordHash = Sha256String(plainPassword);
            } else {
                user.passwordHash = it->second.passwordHash;
            }
        } else if (plainPassword.empty()) {
            error = "新用户必须填写密码";
            return false;
        } else {
            user.passwordHash = Sha256String(plainPassword);
        }
        users_[user.username] = user;
        SaveUsersLocked();
        EnsureUserDirsLocked(user);
        AppendLog("admin", "save_user", user.username);
        return true;
    }

    bool DeleteUser(const std::string& username, std::string& error) {
        if (username.empty() || username == "admin") {
            error = "admin 用户不能删除";
            return false;
        }
        std::lock_guard lock(mu_);
        if (!users_.erase(username)) {
            error = "用户不存在";
            return false;
        }
        SaveUsersLocked();
        AppendLog("admin", "delete_user", username);
        return true;
    }

    std::wstring ReadLogs() const {
        return Utf8ToWide(ReadFileUtf8(logFile_));
    }

private:
    std::filesystem::path dataDir_;
    std::filesystem::path rootDir_;
    std::filesystem::path usersFile_;
    std::filesystem::path logFile_;

    mutable std::mutex mu_;
    mutable std::mutex logMu_;
    std::unordered_map<std::string, UserRecord> users_;
    std::unordered_map<std::uint32_t, SessionInfo> sessions_;
    std::vector<SOCKET> clientSockets_;

    std::atomic<bool> running_{false};
    std::atomic<int> activeClients_{0};
    std::atomic<int> activeTransfers_{0};
    std::atomic<std::uint32_t> nextSession_{1};

    SOCKET listenSock_ = INVALID_SOCKET;
    std::thread loopThread_;
    int port_ = kDefaultPort;

    void EnsureLayout() {
        std::filesystem::create_directories(rootDir_ / L"public");
        std::filesystem::create_directories(rootDir_ / L"download");
        std::filesystem::create_directories(rootDir_ / L"upload");
        std::filesystem::create_directories(rootDir_ / L"users");
        if (!std::filesystem::exists(rootDir_ / L"public" / L"welcome.txt")) {
            std::ofstream(rootDir_ / L"public" / L"welcome.txt") << "Welcome to FDS.\n";
        }
        LoadUsersLocked();
    }

    void EnsureUserDirsLocked(const UserRecord& user) {
        if (user.admin) {
            return;
        }
        std::filesystem::create_directories(rootDir_ / L"users" / Utf8ToWide(user.username));
        std::filesystem::create_directories(rootDir_ / L"upload" / Utf8ToWide(user.username));
    }

    void LoadUsersLocked() {
        std::lock_guard lock(mu_);
        users_.clear();
        auto users = LoadUsers(usersFile_);
        if (users.empty()) {
            users.push_back(MakeDefaultUser("admin", "admin123", true));
            users.push_back(MakeDefaultUser("demo", "demo123", false));
            SaveUsers(usersFile_, users);
        }
        for (const auto& user : users) {
            users_[user.username] = user;
            EnsureUserDirsLocked(user);
        }
    }

    void SaveUsersLocked() {
        std::vector<UserRecord> users;
        for (const auto& [_, user] : users_) {
            users.push_back(user);
        }
        SaveUsers(usersFile_, users);
    }

    void AppendLog(const std::string& user, const std::string& action, const std::string& detail) const {
        std::lock_guard lock(logMu_);
        std::ofstream out(logFile_, std::ios::app);
        out << MakeLine({NowString(), user, action, detail});
    }

    std::optional<SessionInfo> FindSession(std::uint32_t sessionId) {
        std::lock_guard lock(mu_);
        const auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool ResolvePath(const SessionInfo& session, const std::string& rawPath, std::uint32_t bit,
                     std::filesystem::path& realPath, std::string& cleanPath, bool allowMissing = false) {
        cleanPath = NormalizeVirtualPath(rawPath.empty() ? session.home : rawPath);
        if (cleanPath.empty()) {
            return false;
        }
        if (!session.admin && !HasPermission(session.rules, cleanPath, bit)) {
            return false;
        }
        realPath = VirtualToReal(rootDir_, cleanPath);
        std::error_code ec;
        if (!allowMissing && !std::filesystem::exists(realPath, ec)) {
            return false;
        }
        return true;
    }

    void ReplyError(SOCKET sock, const NetPacket& req, const std::string& message) {
        SendPacket(sock, Cmd::Error, req.seq, req.session, SerializePairs({{"message", message}}));
    }

    void ReplyOk(SOCKET sock, const NetPacket& req, const std::map<std::string, std::string>& data = {}) {
        SendPacket(sock, Cmd::Ok, req.seq, req.session, SerializePairs(data));
    }

    void HandleLogin(SOCKET sock, const NetPacket& req) {
        const auto pairs = ParsePairs(req.body);
        const bool anonymous = pairs.contains("anonymous") && pairs.at("anonymous") == "1";

        SessionInfo session;
        if (anonymous) {
            session.username = "anonymous";
            session.home = "/public";
            session.rules = AnonymousRules();
            session.anonymous = true;
        } else {
            const auto userIt = pairs.find("username");
            const auto passIt = pairs.find("password");
            if (userIt == pairs.end() || passIt == pairs.end()) {
                ReplyError(sock, req, "缺少登录参数");
                return;
            }
            std::lock_guard lock(mu_);
            const auto it = users_.find(userIt->second);
            if (it == users_.end() || !it->second.enabled || it->second.passwordHash != Sha256String(passIt->second)) {
                AppendLog(userIt->second, "login_fail", "invalid credential");
                ReplyError(sock, req, "用户名或密码错误");
                return;
            }
            session.username = it->second.username;
            session.home = it->second.home;
            session.rules = it->second.rules;
            session.admin = it->second.admin;
        }

        session.id = nextSession_++;
        {
            std::lock_guard lock(mu_);
            sessions_[session.id] = session;
        }
        AppendLog(session.username, "login_ok", session.home);
        SendPacket(sock, Cmd::LoginOk, req.seq, session.id, SerializePairs({
            {"session", std::to_string(session.id)},
            {"username", session.username},
            {"home", session.home},
            {"admin", session.admin ? "1" : "0"},
        }));
    }

    void HandleList(SOCKET sock, const NetPacket& req) {
        auto session = FindSession(req.session);
        if (!session) {
            ReplyError(sock, req, "会话失效");
            return;
        }
        const auto pairs = ParsePairs(req.body);
        std::filesystem::path real;
        std::string virt;
        if (!ResolvePath(*session, pairs.contains("path") ? pairs.at("path") : session->home, PermRead, real, virt)) {
            AppendLog(session->username, "deny", "list " + (pairs.contains("path") ? pairs.at("path") : ""));
            ReplyError(sock, req, "目录不可访问");
            return;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(real, ec)) {
            ReplyError(sock, req, "不是目录");
            return;
        }
        std::string body = MakeLine({"PWD", virt});
        for (const auto& item : EnumerateDirectory(real, virt)) {
            body += MakeLine({
                "E", item.name, item.path, item.isDir ? "1" : "0",
                std::to_string(item.size), item.mtime
            });
        }
        SendPacket(sock, Cmd::ListResult, req.seq, req.session, body);
    }

    void HandleMakeDir(SOCKET sock, const NetPacket& req) {
        auto session = FindSession(req.session);
        if (!session) {
            ReplyError(sock, req, "会话失效");
            return;
        }
        const auto pairs = ParsePairs(req.body);
        std::filesystem::path real;
        std::string virt;
        if (!pairs.contains("path") ||
            !ResolvePath(*session, pairs.at("path"), PermWrite, real, virt, true)) {
            ReplyError(sock, req, "目录不可写");
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories(real, ec);
        if (ec) {
            ReplyError(sock, req, "创建目录失败");
            return;
        }
        AppendLog(session->username, "mkdir", virt);
        ReplyOk(sock, req);
    }

    void HandleRemove(SOCKET sock, const NetPacket& req) {
        auto session = FindSession(req.session);
        if (!session) {
            ReplyError(sock, req, "会话失效");
            return;
        }
        const auto pairs = ParsePairs(req.body);
        std::filesystem::path real;
        std::string virt;
        if (!pairs.contains("path") ||
            !ResolvePath(*session, pairs.at("path"), PermDelete, real, virt)) {
            ReplyError(sock, req, "没有删除权限");
            return;
        }
        std::error_code ec;
        std::filesystem::remove_all(real, ec);
        if (ec) {
            ReplyError(sock, req, "删除失败");
            return;
        }
        AppendLog(session->username, "remove", virt);
        ReplyOk(sock, req);
    }

    void HandleRename(SOCKET sock, const NetPacket& req) {
        auto session = FindSession(req.session);
        if (!session) {
            ReplyError(sock, req, "会话失效");
            return;
        }
        const auto pairs = ParsePairs(req.body);
        if (!pairs.contains("path") || !pairs.contains("new_name")) {
            ReplyError(sock, req, "缺少参数");
            return;
        }
        auto newName = Trim(pairs.at("new_name"));
        if (newName.empty() || newName.find('/') != std::string::npos || newName.find('\\') != std::string::npos) {
            ReplyError(sock, req, "新名称非法");
            return;
        }
        std::filesystem::path real;
        std::string virt;
        if (!ResolvePath(*session, pairs.at("path"), PermRename, real, virt)) {
            ReplyError(sock, req, "没有重命名权限");
            return;
        }
        std::filesystem::path target = real.parent_path() / Utf8ToWide(newName);
        std::error_code ec;
        std::filesystem::rename(real, target, ec);
        if (ec) {
            ReplyError(sock, req, "重命名失败");
            return;
        }
        AppendLog(session->username, "rename", virt + " -> " + newName);
        ReplyOk(sock, req);
    }

    void HandleLogout(const NetPacket& req) {
        std::lock_guard lock(mu_);
        sessions_.erase(req.session);
    }

    void HandleCommand(SOCKET sock, const NetPacket& req, bool& removeSocket, bool& keepSocketOpen) {
        switch (req.cmd) {
            case Cmd::Login: HandleLogin(sock, req); break;
            case Cmd::List: HandleList(sock, req); break;
            case Cmd::MakeDir: HandleMakeDir(sock, req); break;
            case Cmd::Remove: HandleRemove(sock, req); break;
            case Cmd::Rename: HandleRename(sock, req); break;
            case Cmd::Logout: HandleLogout(req); removeSocket = true; break;
            case Cmd::Ping: ReplyOk(sock, req); break;
            case Cmd::UploadBegin:
                removeSocket = true;
                keepSocketOpen = true;
                SpawnUploadWorker(sock, req);
                break;
            case Cmd::DownloadBegin:
                removeSocket = true;
                keepSocketOpen = true;
                SpawnDownloadWorker(sock, req);
                break;
            default:
                ReplyError(sock, req, "未知命令");
                break;
        }
    }

    void SpawnUploadWorker(SOCKET sock, NetPacket req) {
        // 文件传输连接交给工作线程，避免阻塞 select() 主循环。
        activeTransfers_++;
        std::thread([this, sock, req] {
            UploadWorker(sock, req);
            activeTransfers_--;
        }).detach();
    }

    void SpawnDownloadWorker(SOCKET sock, NetPacket req) {
        activeTransfers_++;
        std::thread([this, sock, req] {
            DownloadWorker(sock, req);
            activeTransfers_--;
        }).detach();
    }

    void UploadWorker(SOCKET sock, const NetPacket& req) {
        auto session = FindSession(req.session);
        if (!session) {
            ReplyError(sock, req, "会话失效");
            CloseSocket(sock);
            return;
        }
        const auto pairs = ParsePairs(req.body);
        if (!pairs.contains("path")) {
            ReplyError(sock, req, "缺少 path");
            CloseSocket(sock);
            return;
        }
        std::filesystem::path real;
        std::string virt;
        if (!ResolvePath(*session, pairs.at("path"), PermWrite, real, virt, true)) {
            ReplyError(sock, req, "没有上传权限");
            CloseSocket(sock);
            return;
        }
        if (!ParentExistsForWrite(real)) {
            ReplyError(sock, req, "目标目录不可写");
            CloseSocket(sock);
            return;
        }
        std::error_code ec;
        std::uint64_t offset = std::filesystem::exists(real, ec) ? std::filesystem::file_size(real, ec) : 0;
        std::ofstream out(real, std::ios::binary | std::ios::app);
        if (!out) {
            ReplyError(sock, req, "无法写入文件");
            CloseSocket(sock);
            return;
        }
        if (!SendPacket(sock, Cmd::UploadReady, req.seq, req.session,
                        SerializePairs({{"offset", std::to_string(offset)}}))) {
            CloseSocket(sock);
            return;
        }

        while (true) {
            NetPacket packet;
            if (!RecvPacket(sock, packet)) {
                break;
            }
            if (packet.cmd == Cmd::UploadData) {
                // 直接按顺序追加，服务端文件大小就是续传偏移量。
                out.write(packet.body.data(), static_cast<std::streamsize>(packet.body.size()));
                offset += static_cast<std::uint64_t>(packet.body.size());
                continue;
            }
            if (packet.cmd == Cmd::UploadEnd) {
                out.flush();
                const auto hash = Sha256File(real);
                SendPacket(sock, Cmd::UploadDone, packet.seq, packet.session,
                           SerializePairs({{"size", std::to_string(offset)}, {"sha256", hash}}));
                AppendLog(session->username, "upload", virt);
                break;
            }
        }
        CloseSocket(sock);
    }

    void DownloadWorker(SOCKET sock, const NetPacket& req) {
        auto session = FindSession(req.session);
        if (!session) {
            ReplyError(sock, req, "会话失效");
            CloseSocket(sock);
            return;
        }
        const auto pairs = ParsePairs(req.body);
        if (!pairs.contains("path")) {
            ReplyError(sock, req, "缺少 path");
            CloseSocket(sock);
            return;
        }
        std::filesystem::path real;
        std::string virt;
        if (!ResolvePath(*session, pairs.at("path"), PermRead, real, virt)) {
            ReplyError(sock, req, "没有下载权限");
            CloseSocket(sock);
            return;
        }
        std::ifstream in(real, std::ios::binary);
        if (!in) {
            ReplyError(sock, req, "文件不存在");
            CloseSocket(sock);
            return;
        }
        std::uint64_t offset = 0;
        if (pairs.contains("offset")) {
            offset = std::strtoull(pairs.at("offset").c_str(), nullptr, 10);
        }
        std::error_code ec;
        const auto total = std::filesystem::file_size(real, ec);
        if (offset > total) {
            offset = 0;
        }
        in.seekg(static_cast<std::streamoff>(offset));
        if (!SendPacket(sock, Cmd::DownloadMeta, req.seq, req.session,
                        SerializePairs({{"size", std::to_string(total)}, {"offset", std::to_string(offset)}}))) {
            CloseSocket(sock);
            return;
        }

        std::vector<char> buffer(kChunkSize);
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = in.gcount();
            if (got <= 0) {
                break;
            }
            if (!SendPacket(sock, Cmd::DownloadData, req.seq, req.session,
                            std::string(buffer.data(), static_cast<std::size_t>(got)))) {
                CloseSocket(sock);
                return;
            }
        }
        SendPacket(sock, Cmd::DownloadDone, req.seq, req.session,
                   SerializePairs({{"size", std::to_string(total)}, {"sha256", Sha256File(real)}}));
        AppendLog(session->username, "download", virt);
        CloseSocket(sock);
    }

    void Loop() {
        while (running_) {
            fd_set rfds;
            FD_ZERO(&rfds);
            SOCKET maxSock = listenSock_;
            if (listenSock_ != INVALID_SOCKET) {
                FD_SET(listenSock_, &rfds);
            }
            std::vector<SOCKET> sockets;
            {
                std::lock_guard lock(mu_);
                sockets = clientSockets_;
            }
            for (auto sock : sockets) {
                FD_SET(sock, &rfds);
                maxSock = std::max(maxSock, sock);
            }
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            const int ready = select(static_cast<int>(maxSock + 1), &rfds, nullptr, nullptr, &tv);
            if (ready <= 0) {
                continue;
            }
            if (listenSock_ != INVALID_SOCKET && FD_ISSET(listenSock_, &rfds)) {
                while (true) {
                    SOCKET client = accept(listenSock_, nullptr, nullptr);
                    if (client == INVALID_SOCKET) {
                        break;
                    }
                    u_long no = 0;
                    ioctlsocket(client, FIONBIO, &no);
                    std::lock_guard lock(mu_);
                    clientSockets_.push_back(client);
                    activeClients_ = static_cast<int>(clientSockets_.size());
                }
            }

            std::vector<SOCKET> toRemove;
            std::set<SOCKET> keepOpen;
            for (auto sock : sockets) {
                if (!FD_ISSET(sock, &rfds)) {
                    continue;
                }
                NetPacket req;
                if (!RecvPacket(sock, req)) {
                    toRemove.push_back(sock);
                    continue;
                }
                bool removeSocket = false;
                bool keepSocketOpen = false;
                HandleCommand(sock, req, removeSocket, keepSocketOpen);
                if (removeSocket) {
                    toRemove.push_back(sock);
                    if (keepSocketOpen) {
                        keepOpen.insert(sock);
                    }
                }
            }

            if (!toRemove.empty()) {
                std::lock_guard lock(mu_);
                for (auto sock : toRemove) {
                    auto it = std::find(clientSockets_.begin(), clientSockets_.end(), sock);
                    if (it != clientSockets_.end()) {
                        auto tmp = *it;
                        clientSockets_.erase(it);
                        if (!keepOpen.contains(sock)) {
                            CloseSocket(tmp);
                        }
                    }
                }
                activeClients_ = static_cast<int>(clientSockets_.size());
            }
        }
    }
};

class ServerWindow {
public:
    explicit ServerWindow(HINSTANCE instance) : instance_(instance) {}

    int Run(int showCmd) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &ServerWindow::WndProc;
        wc.hInstance = instance_;
        wc.lpszClassName = L"FdsServerWindow";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);

        hwnd_ = CreateWindowW(wc.lpszClassName, L"FDS 服务端 / 管理面板",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 980, 680,
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
    HWND status_{};
    HWND portEdit_{};
    HWND startBtn_{};
    HWND adminPass_{};
    HWND userList_{};
    HWND userName_{};
    HWND userPass_{};
    HWND userHome_{};
    HWND userRules_{};
    HWND userEnabled_{};
    HWND userAdmin_{};
    HWND saveBtn_{};
    HWND deleteBtn_{};
    HWND logs_{};
    bool unlocked_ = false;
    ServerCore core_;

    void BuildUi() {
        CreateWindowW(L"STATIC", L"监听端口", WS_CHILD | WS_VISIBLE, 20, 20, 80, 24, hwnd_, nullptr, instance_, nullptr);
        portEdit_ = CreateWindowW(L"EDIT", L"9527", WS_CHILD | WS_VISIBLE | WS_BORDER, 100, 18, 90, 24, hwnd_, reinterpret_cast<HMENU>(IDC_PORT), instance_, nullptr);
        startBtn_ = CreateWindowW(L"BUTTON", L"启动服务", WS_CHILD | WS_VISIBLE, 210, 16, 100, 28, hwnd_, reinterpret_cast<HMENU>(IDC_START), instance_, nullptr);
        status_ = CreateWindowW(L"STATIC", L"服务未启动", WS_CHILD | WS_VISIBLE, 330, 20, 600, 24, hwnd_, reinterpret_cast<HMENU>(IDC_STATUS), instance_, nullptr);

        CreateWindowW(L"STATIC", L"管理员密码", WS_CHILD | WS_VISIBLE, 20, 60, 80, 24, hwnd_, nullptr, instance_, nullptr);
        adminPass_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD, 100, 58, 140, 24, hwnd_, reinterpret_cast<HMENU>(IDC_ADMIN_PASS), instance_, nullptr);
        CreateWindowW(L"BUTTON", L"管理员登录", WS_CHILD | WS_VISIBLE, 260, 56, 100, 28, hwnd_, reinterpret_cast<HMENU>(IDC_ADMIN_LOGIN), instance_, nullptr);

        userList_ = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY, 20, 110, 220, 480, hwnd_, reinterpret_cast<HMENU>(IDC_USER_LIST), instance_, nullptr);
        CreateWindowW(L"STATIC", L"用户名", WS_CHILD | WS_VISIBLE, 260, 110, 70, 24, hwnd_, nullptr, instance_, nullptr);
        userName_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 340, 108, 180, 24, hwnd_, reinterpret_cast<HMENU>(IDC_USER_NAME), instance_, nullptr);
        CreateWindowW(L"STATIC", L"密码", WS_CHILD | WS_VISIBLE, 260, 146, 70, 24, hwnd_, nullptr, instance_, nullptr);
        userPass_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD, 340, 144, 180, 24, hwnd_, reinterpret_cast<HMENU>(IDC_USER_PASS), instance_, nullptr);
        CreateWindowW(L"STATIC", L"主目录", WS_CHILD | WS_VISIBLE, 260, 182, 70, 24, hwnd_, nullptr, instance_, nullptr);
        userHome_ = CreateWindowW(L"EDIT", L"/users/demo", WS_CHILD | WS_VISIBLE | WS_BORDER, 340, 180, 280, 24, hwnd_, reinterpret_cast<HMENU>(IDC_USER_HOME), instance_, nullptr);
        CreateWindowW(L"STATIC", L"权限规则", WS_CHILD | WS_VISIBLE, 260, 218, 70, 24, hwnd_, nullptr, instance_, nullptr);
        userRules_ = CreateWindowW(L"EDIT", L"/public:R;/download:R", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 340, 216, 280, 84, hwnd_, reinterpret_cast<HMENU>(IDC_USER_RULES), instance_, nullptr);
        userEnabled_ = CreateWindowW(L"BUTTON", L"启用账户", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 340, 312, 100, 24, hwnd_, reinterpret_cast<HMENU>(IDC_USER_ENABLED), instance_, nullptr);
        userAdmin_ = CreateWindowW(L"BUTTON", L"管理员", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 450, 312, 100, 24, hwnd_, reinterpret_cast<HMENU>(IDC_USER_ADMIN), instance_, nullptr);
        saveBtn_ = CreateWindowW(L"BUTTON", L"保存/更新用户", WS_CHILD | WS_VISIBLE, 340, 348, 130, 28, hwnd_, reinterpret_cast<HMENU>(IDC_USER_SAVE), instance_, nullptr);
        deleteBtn_ = CreateWindowW(L"BUTTON", L"删除用户", WS_CHILD | WS_VISIBLE, 490, 348, 130, 28, hwnd_, reinterpret_cast<HMENU>(IDC_USER_DELETE), instance_, nullptr);

        CreateWindowW(L"STATIC", L"审计日志", WS_CHILD | WS_VISIBLE, 650, 110, 120, 24, hwnd_, nullptr, instance_, nullptr);
        logs_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                              650, 138, 290, 452, hwnd_, reinterpret_cast<HMENU>(IDC_LOGS), instance_, nullptr);

        EnableAdminControls(false);
        SetTimer(hwnd_, 1, 1000, nullptr);
        RefreshUi();
    }

    void EnableAdminControls(bool enabled) {
        unlocked_ = enabled;
        EnableWindow(userList_, enabled);
        EnableWindow(userName_, enabled);
        EnableWindow(userPass_, enabled);
        EnableWindow(userHome_, enabled);
        EnableWindow(userRules_, enabled);
        EnableWindow(userEnabled_, enabled);
        EnableWindow(userAdmin_, enabled);
        EnableWindow(saveBtn_, enabled);
        EnableWindow(deleteBtn_, enabled);
    }

    void RefreshUi() {
        SetText(status_, core_.StatusText());
        SetText(logs_, core_.ReadLogs());
        if (!unlocked_) {
            return;
        }
        const auto users = core_.SnapshotUsers();
        const int selected = ListBox_GetCurSel(userList_);
        std::wstring keep;
        if (selected != LB_ERR) {
            wchar_t name[256]{};
            ListBox_GetText(userList_, selected, name);
            keep = name;
        }
        SendMessageW(userList_, LB_RESETCONTENT, 0, 0);
        int selectIndex = 0;
        for (int i = 0; i < static_cast<int>(users.size()); ++i) {
            const auto name = Utf8ToWide(users[i].username);
            ListBox_AddString(userList_, name.c_str());
            if (name == keep) {
                selectIndex = i;
            }
        }
        if (!users.empty()) {
            ListBox_SetCurSel(userList_, selectIndex);
            FillFromSelection();
        }
    }

    void FillFromSelection() {
        const int index = ListBox_GetCurSel(userList_);
        if (index == LB_ERR) {
            return;
        }
        wchar_t buffer[256]{};
        ListBox_GetText(userList_, index, buffer);
        const auto users = core_.SnapshotUsers();
        for (const auto& user : users) {
            if (Utf8ToWide(user.username) == buffer) {
                SetText(userName_, Utf8ToWide(user.username));
                SetText(userPass_, L"");
                SetText(userHome_, Utf8ToWide(user.home));
                SetText(userRules_, Utf8ToWide(user.ruleSpec));
                Button_SetCheck(userEnabled_, user.enabled ? BST_CHECKED : BST_UNCHECKED);
                Button_SetCheck(userAdmin_, user.admin ? BST_CHECKED : BST_UNCHECKED);
                break;
            }
        }
    }

    void ToggleServer() {
        if (core_.IsRunning()) {
            core_.Stop();
            SetWindowTextW(startBtn_, L"启动服务");
            RefreshUi();
            return;
        }
        const int port = _wtoi(GetText(portEdit_).c_str());
        std::string error;
        if (!core_.Start(port > 0 ? port : kDefaultPort, error)) {
            Alert(hwnd_, Utf8ToWide(error));
            return;
        }
        SetWindowTextW(startBtn_, L"停止服务");
        RefreshUi();
    }

    void DoAdminLogin() {
        if (!core_.VerifyAdmin(WideToUtf8(GetText(adminPass_)))) {
            Alert(hwnd_, L"管理员密码错误。默认账号: admin / admin123");
            return;
        }
        EnableAdminControls(true);
        RefreshUi();
    }

    void SaveUser() {
        UserRecord user;
        user.username = WideToUtf8(GetText(userName_));
        user.home = WideToUtf8(GetText(userHome_));
        user.ruleSpec = WideToUtf8(GetText(userRules_));
        user.enabled = Button_GetCheck(userEnabled_) == BST_CHECKED;
        user.admin = Button_GetCheck(userAdmin_) == BST_CHECKED;
        std::string error;
        if (!core_.UpsertUser(user, WideToUtf8(GetText(userPass_)), error)) {
            Alert(hwnd_, Utf8ToWide(error));
            return;
        }
        RefreshUi();
    }

    void RemoveUser() {
        std::string error;
        if (!core_.DeleteUser(WideToUtf8(GetText(userName_)), error)) {
            Alert(hwnd_, Utf8ToWide(error));
            return;
        }
        RefreshUi();
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
                self->RefreshUi();
                return 0;
            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case IDC_START: self->ToggleServer(); return 0;
                    case IDC_ADMIN_LOGIN: self->DoAdminLogin(); return 0;
                    case IDC_USER_SAVE: self->SaveUser(); return 0;
                    case IDC_USER_DELETE: self->RemoveUser(); return 0;
                    case IDC_USER_LIST:
                        if (HIWORD(wParam) == LBN_SELCHANGE) {
                            self->FillFromSelection();
                        }
                        return 0;
                    default: break;
                }
                break;
            case WM_DESTROY:
                KillTimer(hwnd, 1);
                self->core_.Stop();
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

int RunHeadless(int argc, wchar_t** argv) {
    int port = kDefaultPort;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring_view(argv[i]) == L"--port" && i + 1 < argc) {
            port = _wtoi(argv[++i]);
        }
    }
    ServerCore core;
    std::string error;
    if (!core.Start(port, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::wcout << L"FDS server is running on port " << port << L". Press Ctrl+C to stop.\n";
    while (true) {
        Sleep(1000);
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    ScopedSockets sockets;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring_view(argv[i]) == L"--headless") {
            return RunHeadless(argc, argv);
        }
    }
    ServerWindow app(GetModuleHandleW(nullptr));
    return app.Run(SW_SHOW);
}
