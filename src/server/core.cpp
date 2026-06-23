#include "core.hpp"

#include <fstream>

namespace fds::serverapp {

namespace {

class SocketGuard {
public:
    explicit SocketGuard(SOCKET sock) : sock_(sock) {}

    ~SocketGuard() {
        CloseSocket(sock_);
    }

private:
    SOCKET sock_;
};

class TransferCounterGuard {
public:
    explicit TransferCounterGuard(std::atomic<int>& counter) : counter_(counter) {
        ++counter_;
    }

    ~TransferCounterGuard() {
        --counter_;
    }

private:
    std::atomic<int>& counter_;
};

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

bool IsValidUserName(const std::string& username) {
    if (username.empty() || username == "." || username == "..") {
        return false;
    }
    return username.find_first_of("\\/\t\r\n") == std::string::npos;
}

std::uint64_t ParseU64(const std::string& value) {
    return value.empty() ? 0 : std::strtoull(value.c_str(), nullptr, 10);
}

}  // namespace

ServerCore::ServerCore()
    : dataDir_(std::filesystem::current_path() / L"data"),
      rootDir_(dataDir_ / L"server_root"),
      usersDbFile_(dataDir_ / L"users.db"),
      legacyUsersFile_(dataDir_ / L"users.tsv"),
      userStore_(usersDbFile_, legacyUsersFile_) {
    EnsureLayout();
}

ServerCore::~ServerCore() {
    Stop();
}

bool ServerCore::Start(int port, std::string& error) {
    if (running_) {
        return true;
    }

    SOCKET listenSocket = CreateListenSocket(port);
    if (listenSocket == INVALID_SOCKET) {
        error = "端口监听失败";
        return false;
    }

    port_ = port;
    listenSock_ = listenSocket;
    running_ = true;
    loopThread_ = std::thread([this] { Loop(); });
    return true;
}

void ServerCore::Stop() {
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
        auto temp = sock;
        CloseSocket(temp);
    }
    clientSockets_.clear();
    sessions_.clear();
    activeClients_ = 0;
}

bool ServerCore::IsRunning() const {
    return running_;
}

std::wstring ServerCore::StatusText() const {
    std::string storageError;
    {
        std::lock_guard lock(mu_);
        storageError = userStoreError_;
    }

    std::ostringstream out;
    out << (running_ ? "运行中" : "已停止") << "  端口 " << port_ << "  连接 " << activeClients_.load() << "  传输 "
        << activeTransfers_.load();
    if (!storageError.empty()) {
        out << "  存储异常";
    }
    return Utf8ToWide(out.str());
}

std::vector<UserRecord> ServerCore::SnapshotUsers() {
    std::lock_guard lock(mu_);
    std::vector<UserRecord> snapshot;
    snapshot.reserve(users_.size());
    for (const auto& [_, user] : users_) {
        snapshot.push_back(user);
    }
    std::sort(snapshot.begin(), snapshot.end(), [](const UserRecord& a, const UserRecord& b) {
        return _stricmp(a.username.c_str(), b.username.c_str()) < 0;
    });
    return snapshot;
}

std::vector<FileEntry> ServerCore::SnapshotAdminDirectory(const std::string& path, std::string& cwd,
                                                          std::string& error) const {
    const auto resolved = ResolveAdminPath(path);
    if (!resolved) {
        error = "目录不可访问";
        return {};
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(resolved->real, ec)) {
        error = "目标不是目录";
        return {};
    }

    cwd = resolved->virtualPath;
    return EnumerateDirectory(resolved->real, resolved->virtualPath);
}

bool ServerCore::AdminMakeDir(const std::string& path, std::string& error) {
    const auto resolved = ResolveAdminPath(path, true);
    if (!resolved) {
        error = "目录不可创建";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(resolved->real, ec);
    if (ec) {
        error = "创建目录失败";
        return false;
    }
    return true;
}

bool ServerCore::AdminRemove(const std::string& path, std::string& error) {
    const auto resolved = ResolveAdminPath(path);
    if (!resolved || resolved->virtualPath == "/") {
        error = "目标不可删除";
        return false;
    }

    std::error_code ec;
    std::filesystem::remove_all(resolved->real, ec);
    if (ec) {
        error = "删除失败";
        return false;
    }
    return true;
}

bool ServerCore::AdminRename(const std::string& path, const std::string& newName, std::string& error) {
    auto cleanName = Trim(newName);
    if (cleanName.empty() || cleanName.find('/') != std::string::npos || cleanName.find('\\') != std::string::npos) {
        error = "新名称无效";
        return false;
    }

    const auto resolved = ResolveAdminPath(path);
    if (!resolved || resolved->virtualPath == "/") {
        error = "目标不可重命名";
        return false;
    }

    std::filesystem::path target = resolved->real.parent_path() / Utf8ToWide(cleanName);
    std::error_code ec;
    std::filesystem::rename(resolved->real, target, ec);
    if (ec) {
        error = "重命名失败";
        return false;
    }
    return true;
}

std::vector<TransferSnapshot> ServerCore::SnapshotTransfers() const {
    std::lock_guard lock(transferMu_);
    auto snapshot = transfers_;
    std::sort(snapshot.begin(), snapshot.end(), [](const TransferSnapshot& a, const TransferSnapshot& b) {
        return a.id > b.id;
    });
    return snapshot;
}

bool ServerCore::UpsertUser(const UserRecord& input, const std::string& plainPassword, std::string& error) {
    auto user = input;
    user.username = Trim(user.username);
    if (!IsValidUserName(user.username)) {
        error = "用户名不能为空，且不能包含斜杠或非法路径";
        return false;
    }
    if (user.home.empty() || NormalizeVirtualPath(user.home).empty()) {
        error = "用户目录无效";
        return false;
    }

    user.home = NormalizeVirtualPath(user.home);
    user.ruleSpec = Trim(user.ruleSpec);
    user.rules = ParseRuleSpec(user.ruleSpec);
    if (user.rules.empty()) {
        error = "权限配置不能为空";
        return false;
    }

    std::lock_guard lock(mu_);
    const auto backup = users_;
    const auto it = users_.find(user.username);
    if (it != users_.end()) {
        user.passwordHash = plainPassword.empty() ? it->second.passwordHash : Sha256String(plainPassword);
    } else if (plainPassword.empty()) {
        error = "新用户必须填写密码";
        return false;
    } else {
        user.passwordHash = Sha256String(plainPassword);
    }

    users_[user.username] = user;
    if (!SaveUsersLocked(error)) {
        users_ = backup;
        return false;
    }

    EnsureUserDirsLocked(user);
    return true;
}

bool ServerCore::DeleteUser(const std::string& username, std::string& error) {
    if (username.empty() || username == "admin") {
        error = "admin 用户不能删除";
        return false;
    }

    std::lock_guard lock(mu_);
    const auto backup = users_;
    if (!users_.erase(username)) {
        error = "用户不存在";
        return false;
    }

    if (!SaveUsersLocked(error)) {
        users_ = backup;
        return false;
    }
    return true;
}

void ServerCore::EnsureLayout() {
    std::filesystem::create_directories(rootDir_ / L"public");
    std::filesystem::create_directories(rootDir_ / L"download");
    std::filesystem::create_directories(rootDir_ / L"upload");
    std::filesystem::create_directories(rootDir_ / L"users");
    if (!std::filesystem::exists(rootDir_ / L"public" / L"welcome.txt")) {
        std::ofstream(rootDir_ / L"public" / L"welcome.txt") << "Welcome to FDS.\n";
    }
    LoadUsersLocked();
}

void ServerCore::EnsureUserDirsLocked(const UserRecord& user) {
    if (user.admin) {
        return;
    }
    std::filesystem::create_directories(rootDir_ / L"users" / Utf8ToWide(user.username));
    std::filesystem::create_directories(rootDir_ / L"upload" / Utf8ToWide(user.username));
}

std::vector<UserRecord> ServerCore::SeedUsers() const {
    return {
        MakeDefaultUser("admin", "admin123", true),
        MakeDefaultUser("demo", "demo123", false),
    };
}

void ServerCore::LoadUsersLocked() {
    std::lock_guard lock(mu_);
    users_.clear();
    userStoreError_.clear();

    std::vector<UserRecord> loadedUsers;
    std::string error;
    if (!userStore_.Load(loadedUsers, SeedUsers(), error)) {
        userStoreError_ = error;
        loadedUsers = SeedUsers();
    }

    for (const auto& user : loadedUsers) {
        users_[user.username] = user;
        EnsureUserDirsLocked(user);
    }
}

bool ServerCore::SaveUsersLocked(std::string& error) {
    std::vector<UserRecord> users;
    users.reserve(users_.size());
    for (const auto& [_, user] : users_) {
        users.push_back(user);
    }

    if (!userStore_.Save(users, error)) {
        userStoreError_ = error;
        return false;
    }

    userStoreError_.clear();
    return true;
}

std::optional<ServerCore::ResolvedPath> ServerCore::ResolveAdminPath(const std::string& rawPath,
                                                                     bool allowMissing) const {
    const auto cleanPath = NormalizeVirtualPath(rawPath.empty() ? "/" : rawPath);
    if (cleanPath.empty()) {
        return std::nullopt;
    }

    const auto realPath = VirtualToReal(rootDir_, cleanPath);
    std::error_code ec;
    if (!allowMissing && !std::filesystem::exists(realPath, ec)) {
        return std::nullopt;
    }

    return ResolvedPath{realPath, cleanPath};
}

std::optional<SessionInfo> ServerCore::FindSession(std::uint32_t sessionId) {
    std::lock_guard lock(mu_);
    const auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<ServerCore::ResolvedPath> ServerCore::ResolvePath(const SessionInfo& session, const std::string& rawPath,
                                                                std::uint32_t bit, bool allowMissing) {
    const auto cleanPath = NormalizeVirtualPath(rawPath.empty() ? session.home : rawPath);
    if (cleanPath.empty()) {
        return std::nullopt;
    }
    if (!session.admin && !HasPermission(session.rules, cleanPath, bit)) {
        return std::nullopt;
    }

    const auto realPath = VirtualToReal(rootDir_, cleanPath);
    std::error_code ec;
    if (!allowMissing && !std::filesystem::exists(realPath, ec)) {
        return std::nullopt;
    }

    return ResolvedPath{realPath, cleanPath};
}

std::uint32_t ServerCore::StartTransfer(const std::string& username, const std::string& direction, const std::string& path,
                                        std::uint64_t total) {
    TransferSnapshot transfer;
    transfer.id = nextTransfer_++;
    transfer.username = username;
    transfer.direction = direction;
    transfer.path = path;
    transfer.status = "运行中";
    transfer.updatedAt = NowString();
    transfer.total = total;

    std::lock_guard lock(transferMu_);
    transfers_.push_back(std::move(transfer));
    if (transfers_.size() > 80) {
        transfers_.erase(transfers_.begin(), transfers_.begin() + static_cast<std::ptrdiff_t>(transfers_.size() - 80));
    }
    return transfers_.back().id;
}

void ServerCore::UpdateTransfer(std::uint32_t id, std::uint64_t done, std::uint64_t total, const std::string& status,
                                const std::string& detail) {
    std::lock_guard lock(transferMu_);
    for (auto& item : transfers_) {
        if (item.id != id) {
            continue;
        }
        item.done = done;
        item.total = total;
        if (!status.empty()) {
            item.status = status;
        }
        if (!detail.empty()) {
            item.detail = detail;
        }
        item.updatedAt = NowString();
        return;
    }
}

void ServerCore::FinishTransfer(std::uint32_t id, const std::string& status, const std::string& detail, std::uint64_t done,
                                std::uint64_t total) {
    UpdateTransfer(id, done, total, status, detail);
}

void ServerCore::ReplyError(SOCKET sock, const NetPacket& req, const std::string& message) {
    SendPacket(sock, Cmd::Error, req.seq, req.session, SerializePairs({{"message", message}}));
}

void ServerCore::ReplyOk(SOCKET sock, const NetPacket& req, const std::map<std::string, std::string>& data) {
    SendPacket(sock, Cmd::Ok, req.seq, req.session, SerializePairs(data));
}

std::string ServerCore::RootLabelForPath(const SessionInfo& session, const std::string& path) const {
    const auto virtualPath = NormalizeVirtualPath(path);
    if (virtualPath == "/public") {
        return "公共目录";
    }
    if (virtualPath == "/download") {
        return "下载目录";
    }
    if (virtualPath == NormalizeVirtualPath("/users/" + session.username)) {
        return "用户目录";
    }
    if (virtualPath == NormalizeVirtualPath("/upload/" + session.username)) {
        return "上传目录";
    }
    if (virtualPath == NormalizeVirtualPath(session.home)) {
        return "主目录";
    }
    return virtualPath == "/" ? "/" : virtualPath.substr(1);
}

void ServerCore::AddRootShortcut(std::vector<FileEntry>& out, std::set<std::string>& seen, const SessionInfo& session,
                                 const std::string& path) const {
    const auto virtualPath = NormalizeVirtualPath(path);
    if (virtualPath.empty() || virtualPath == "/" || seen.contains(virtualPath)) {
        return;
    }
    if (!session.admin && !HasPermission(session.rules, virtualPath, PermRead)) {
        return;
    }

    const auto realPath = VirtualToReal(rootDir_, virtualPath);
    std::error_code ec;
    if (!std::filesystem::is_directory(realPath, ec)) {
        return;
    }

    FileEntry entry;
    entry.name = RootLabelForPath(session, virtualPath);
    entry.path = virtualPath;
    entry.isDir = true;
    entry.size = 0;
    const auto lastWrite = std::filesystem::last_write_time(realPath, ec);
    entry.mtime = ec ? "" : FileTimeString(lastWrite);
    seen.insert(virtualPath);
    out.push_back(std::move(entry));
}

std::vector<FileEntry> ServerCore::EnumerateSessionRoot(const SessionInfo& session) const {
    if (session.admin) {
        auto entries = EnumerateDirectory(rootDir_, "/");
        for (auto& entry : entries) {
            entry.name = RootLabelForPath(session, entry.path);
        }
        return entries;
    }

    std::vector<FileEntry> entries;
    std::set<std::string> seen;
    AddRootShortcut(entries, seen, session, session.home);
    AddRootShortcut(entries, seen, session, "/public");
    AddRootShortcut(entries, seen, session, "/download");
    AddRootShortcut(entries, seen, session, "/users/" + session.username);
    AddRootShortcut(entries, seen, session, "/upload/" + session.username);
    for (const auto& rule : session.rules) {
        if ((rule.bits & PermRead) == PermRead) {
            AddRootShortcut(entries, seen, session, rule.prefix);
        }
    }
    return entries;
}

std::string ServerCore::BuildListResponse(const std::string& cwd, const std::vector<FileEntry>& entries) const {
    std::string body = MakeLine({"PWD", cwd});
    for (const auto& entry : entries) {
        body += MakeLine({
            "E",
            entry.name,
            entry.path,
            entry.isDir ? "1" : "0",
            std::to_string(entry.size),
            entry.mtime,
        });
    }
    return body;
}

void ServerCore::HandleLogin(SOCKET sock, const NetPacket& req) {
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

    SendPacket(sock, Cmd::LoginOk, req.seq, session.id,
               SerializePairs({
                   {"session", std::to_string(session.id)},
                   {"username", session.username},
                   {"home", session.home},
                   {"admin", session.admin ? "1" : "0"},
               }));
}

void ServerCore::HandleList(SOCKET sock, const NetPacket& req) {
    auto session = FindSession(req.session);
    if (!session) {
        ReplyError(sock, req, "会话失效");
        return;
    }

    const auto pairs = ParsePairs(req.body);
    const std::string requested = pairs.contains("path") ? pairs.at("path") : session->home;
    const auto normalized = NormalizeVirtualPath(requested.empty() ? session->home : requested);
    if (normalized.empty()) {
        ReplyError(sock, req, "目录不可访问");
        return;
    }

    if (normalized == "/") {
        SendPacket(sock, Cmd::ListResult, req.seq, req.session, BuildListResponse("/", EnumerateSessionRoot(*session)));
        return;
    }

    const auto resolved = ResolvePath(*session, normalized, PermRead);
    if (!resolved) {
        ReplyError(sock, req, "目录不可访问");
        return;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(resolved->real, ec)) {
        ReplyError(sock, req, "目标不是目录");
        return;
    }

    SendPacket(sock, Cmd::ListResult, req.seq, req.session,
               BuildListResponse(resolved->virtualPath, EnumerateDirectory(resolved->real, resolved->virtualPath)));
}

void ServerCore::HandleMakeDir(SOCKET sock, const NetPacket& req) {
    auto session = FindSession(req.session);
    if (!session) {
        ReplyError(sock, req, "会话失效");
        return;
    }

    const auto pairs = ParsePairs(req.body);
    if (!pairs.contains("path")) {
        ReplyError(sock, req, "缺少 path");
        return;
    }

    const auto resolved = ResolvePath(*session, pairs.at("path"), PermWrite, true);
    if (!resolved) {
        ReplyError(sock, req, "目录不可写");
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(resolved->real, ec);
    if (ec) {
        ReplyError(sock, req, "创建目录失败");
        return;
    }

    ReplyOk(sock, req);
}

void ServerCore::HandleRemove(SOCKET sock, const NetPacket& req) {
    auto session = FindSession(req.session);
    if (!session) {
        ReplyError(sock, req, "会话失效");
        return;
    }

    const auto pairs = ParsePairs(req.body);
    if (!pairs.contains("path")) {
        ReplyError(sock, req, "缺少 path");
        return;
    }

    const auto resolved = ResolvePath(*session, pairs.at("path"), PermDelete);
    if (!resolved) {
        ReplyError(sock, req, "没有删除权限");
        return;
    }

    std::error_code ec;
    std::filesystem::remove_all(resolved->real, ec);
    if (ec) {
        ReplyError(sock, req, "删除失败");
        return;
    }

    ReplyOk(sock, req);
}

void ServerCore::HandleRename(SOCKET sock, const NetPacket& req) {
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

    const auto resolved = ResolvePath(*session, pairs.at("path"), PermRename);
    if (!resolved) {
        ReplyError(sock, req, "没有重命名权限");
        return;
    }

    std::filesystem::path target = resolved->real.parent_path() / Utf8ToWide(newName);
    std::error_code ec;
    std::filesystem::rename(resolved->real, target, ec);
    if (ec) {
        ReplyError(sock, req, "重命名失败");
        return;
    }

    ReplyOk(sock, req);
}

void ServerCore::HandleLogout(const NetPacket& req) {
    std::lock_guard lock(mu_);
    sessions_.erase(req.session);
}

void ServerCore::HandleCommand(SOCKET sock, const NetPacket& req, bool& removeSocket, bool& keepSocketOpen) {
    switch (req.cmd) {
        case Cmd::Login:
            HandleLogin(sock, req);
            return;
        case Cmd::List:
            HandleList(sock, req);
            return;
        case Cmd::MakeDir:
            HandleMakeDir(sock, req);
            return;
        case Cmd::Remove:
            HandleRemove(sock, req);
            return;
        case Cmd::Rename:
            HandleRename(sock, req);
            return;
        case Cmd::Logout:
            HandleLogout(req);
            removeSocket = true;
            return;
        case Cmd::Ping:
            ReplyOk(sock, req);
            return;
        case Cmd::UploadBegin:
            removeSocket = true;
            keepSocketOpen = true;
            SpawnUploadWorker(sock, req);
            return;
        case Cmd::DownloadBegin:
            removeSocket = true;
            keepSocketOpen = true;
            SpawnDownloadWorker(sock, req);
            return;
        default:
            ReplyError(sock, req, "未知命令");
            return;
    }
}

void ServerCore::SpawnUploadWorker(SOCKET sock, NetPacket req) {
    std::thread([this, sock, req] {
        TransferCounterGuard counter(activeTransfers_);
        UploadWorker(sock, req);
    }).detach();
}

void ServerCore::SpawnDownloadWorker(SOCKET sock, NetPacket req) {
    std::thread([this, sock, req] {
        TransferCounterGuard counter(activeTransfers_);
        DownloadWorker(sock, req);
    }).detach();
}

void ServerCore::UploadWorker(SOCKET sock, const NetPacket& req) {
    SocketGuard guard(sock);

    auto session = FindSession(req.session);
    if (!session) {
        ReplyError(sock, req, "会话失效");
        return;
    }

    const auto beginMeta = ParsePairs(req.body);
    const auto pathIt = beginMeta.find("path");
    if (pathIt == beginMeta.end()) {
        ReplyError(sock, req, "缺少 path");
        return;
    }

    const auto resolved = ResolvePath(*session, pathIt->second, PermWrite, true);
    if (!resolved) {
        ReplyError(sock, req, "没有上传权限");
        return;
    }
    if (!ParentExistsForWrite(resolved->real)) {
        ReplyError(sock, req, "目标目录不可写");
        return;
    }

    std::error_code ec;
    std::uint64_t offset = std::filesystem::exists(resolved->real, ec) ? std::filesystem::file_size(resolved->real, ec) : 0;
    const std::uint64_t requestedTotal = beginMeta.contains("size") ? ParseU64(beginMeta.at("size")) : 0;
    const std::uint64_t total = std::max(requestedTotal, offset);

    std::ofstream out(resolved->real, std::ios::binary | std::ios::app);
    if (!out) {
        ReplyError(sock, req, "无法写入文件");
        return;
    }

    const auto transferId = StartTransfer(session->username, "上传", resolved->virtualPath, total);
    UpdateTransfer(transferId, offset, total);

    if (!SendPacket(sock, Cmd::UploadReady, req.seq, req.session, SerializePairs({{"offset", std::to_string(offset)}}))) {
        FinishTransfer(transferId, "中断", "准备响应失败", offset, total);
        return;
    }

    std::string expectedHash = beginMeta.contains("sha256") ? beginMeta.at("sha256") : "";
    while (true) {
        NetPacket packet;
        if (!RecvPacket(sock, packet)) {
            FinishTransfer(transferId, "中断", "连接断开", offset, total);
            return;
        }

        if (packet.cmd == Cmd::UploadData) {
            out.write(packet.body.data(), static_cast<std::streamsize>(packet.body.size()));
            if (!out) {
                ReplyError(sock, packet, "写入文件失败");
                FinishTransfer(transferId, "失败", "写入文件失败", offset, total);
                return;
            }
            offset += static_cast<std::uint64_t>(packet.body.size());
            UpdateTransfer(transferId, offset, std::max(total, offset));
            continue;
        }

        if (packet.cmd != Cmd::UploadEnd) {
            ReplyError(sock, packet, "上传命令顺序错误");
            FinishTransfer(transferId, "失败", "命令顺序错误", offset, total);
            return;
        }

        out.flush();
        const auto endMeta = ParsePairs(packet.body);
        if (const auto it = endMeta.find("sha256"); it != endMeta.end()) {
            expectedHash = it->second;
        }

        const auto finalHash = Sha256File(resolved->real);
        if (!expectedHash.empty() && finalHash != expectedHash) {
            ReplyError(sock, packet, "上传后校验失败");
            FinishTransfer(transferId, "失败", "校验失败", offset, std::max(total, offset));
            return;
        }

        if (!SendPacket(sock, Cmd::UploadDone, packet.seq, packet.session,
                        SerializePairs({{"size", std::to_string(offset)}, {"sha256", finalHash}}))) {
            FinishTransfer(transferId, "中断", "完成响应失败", offset, std::max(total, offset));
            return;
        }

        FinishTransfer(transferId, "完成", "校验通过", offset, std::max(total, offset));
        return;
    }
}

void ServerCore::DownloadWorker(SOCKET sock, const NetPacket& req) {
    SocketGuard guard(sock);

    auto session = FindSession(req.session);
    if (!session) {
        ReplyError(sock, req, "会话失效");
        return;
    }

    const auto pairs = ParsePairs(req.body);
    const auto pathIt = pairs.find("path");
    if (pathIt == pairs.end()) {
        ReplyError(sock, req, "缺少 path");
        return;
    }

    const auto resolved = ResolvePath(*session, pathIt->second, PermRead);
    if (!resolved) {
        ReplyError(sock, req, "没有下载权限");
        return;
    }

    std::ifstream in(resolved->real, std::ios::binary);
    if (!in) {
        ReplyError(sock, req, "文件不存在");
        return;
    }

    std::uint64_t offset = pairs.contains("offset") ? ParseU64(pairs.at("offset")) : 0;
    std::error_code ec;
    const auto total = std::filesystem::file_size(resolved->real, ec);
    if (offset > total) {
        offset = 0;
    }

    const auto transferId = StartTransfer(session->username, "下载", resolved->virtualPath, total);
    UpdateTransfer(transferId, offset, total);

    in.seekg(static_cast<std::streamoff>(offset));
    if (!SendPacket(sock, Cmd::DownloadMeta, req.seq, req.session,
                    SerializePairs({{"size", std::to_string(total)}, {"offset", std::to_string(offset)}}))) {
        FinishTransfer(transferId, "中断", "元数据响应失败", offset, total);
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
            FinishTransfer(transferId, "中断", "发送数据失败", offset, total);
            return;
        }

        offset += static_cast<std::uint64_t>(got);
        UpdateTransfer(transferId, offset, total);
    }

    if (!SendPacket(sock, Cmd::DownloadDone, req.seq, req.session,
                    SerializePairs({{"size", std::to_string(total)}, {"sha256", Sha256File(resolved->real)}}))) {
        FinishTransfer(transferId, "中断", "完成响应失败", offset, total);
        return;
    }

    FinishTransfer(transferId, "完成", "传输完成", offset, total);
}

void ServerCore::Loop() {
    while (running_) {
        fd_set readSet;
        FD_ZERO(&readSet);

        SOCKET maxSocket = listenSock_;
        if (listenSock_ != INVALID_SOCKET) {
            FD_SET(listenSock_, &readSet);
        }

        std::vector<SOCKET> sockets;
        {
            std::lock_guard lock(mu_);
            sockets = clientSockets_;
        }

        for (auto sock : sockets) {
            FD_SET(sock, &readSet);
            maxSocket = std::max(maxSocket, sock);
        }

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        const int ready = select(static_cast<int>(maxSocket + 1), &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue;
        }

        if (listenSock_ != INVALID_SOCKET && FD_ISSET(listenSock_, &readSet)) {
            while (true) {
                SOCKET client = accept(listenSock_, nullptr, nullptr);
                if (client == INVALID_SOCKET) {
                    break;
                }
                u_long blocking = 0;
                ioctlsocket(client, FIONBIO, &blocking);
                std::lock_guard lock(mu_);
                clientSockets_.push_back(client);
                activeClients_ = static_cast<int>(clientSockets_.size());
            }
        }

        std::vector<SOCKET> toRemove;
        std::set<SOCKET> keepOpen;
        for (auto sock : sockets) {
            if (!FD_ISSET(sock, &readSet)) {
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
                const auto it = std::find(clientSockets_.begin(), clientSockets_.end(), sock);
                if (it == clientSockets_.end()) {
                    continue;
                }
                auto temp = *it;
                clientSockets_.erase(it);
                if (!keepOpen.contains(sock)) {
                    CloseSocket(temp);
                }
            }
            activeClients_ = static_cast<int>(clientSockets_.size());
        }
    }
}

}  // namespace fds::serverapp
