#pragma once

#include "shared/common.hpp"
#include "user_store.hpp"

#include <unordered_map>

namespace fds::serverapp {

struct TransferSnapshot {
    std::uint32_t id = 0;
    std::string username;
    std::string direction;
    std::string path;
    std::string status;
    std::string detail;
    std::string updatedAt;
    std::uint64_t done = 0;
    std::uint64_t total = 0;
};

class ServerCore {
public:
    ServerCore();
    ~ServerCore();

    bool Start(int port, std::string& error);
    void Stop();

    bool IsRunning() const;
    std::wstring StatusText() const;
    std::vector<UserRecord> SnapshotUsers();
    std::vector<FileEntry> SnapshotAdminDirectory(const std::string& path, std::string& cwd, std::string& error) const;
    bool AdminMakeDir(const std::string& path, std::string& error);
    bool AdminRemove(const std::string& path, std::string& error);
    bool AdminRename(const std::string& path, const std::string& newName, std::string& error);
    std::vector<TransferSnapshot> SnapshotTransfers() const;
    bool UpsertUser(const UserRecord& input, const std::string& plainPassword, std::string& error);
    bool DeleteUser(const std::string& username, std::string& error);

private:
    struct ResolvedPath {
        std::filesystem::path real;
        std::string virtualPath;
    };

    void EnsureLayout();
    void EnsureUserDirsLocked(const UserRecord& user);
    std::vector<UserRecord> SeedUsers() const;
    void LoadUsersLocked();
    bool SaveUsersLocked(std::string& error);
    std::optional<ResolvedPath> ResolveAdminPath(const std::string& rawPath, bool allowMissing = false) const;
    std::optional<SessionInfo> FindSession(std::uint32_t sessionId);
    std::optional<ResolvedPath> ResolvePath(const SessionInfo& session, const std::string& rawPath, std::uint32_t bit,
                                            bool allowMissing = false);
    std::uint32_t StartTransfer(const std::string& username, const std::string& direction, const std::string& path,
                                std::uint64_t total);
    void UpdateTransfer(std::uint32_t id, std::uint64_t done, std::uint64_t total, const std::string& status = {},
                        const std::string& detail = {});
    void FinishTransfer(std::uint32_t id, const std::string& status, const std::string& detail, std::uint64_t done,
                        std::uint64_t total);

    void ReplyError(SOCKET sock, const NetPacket& req, const std::string& message);
    void ReplyOk(SOCKET sock, const NetPacket& req, const std::map<std::string, std::string>& data = {});

    std::string RootLabelForPath(const SessionInfo& session, const std::string& path) const;
    void AddRootShortcut(std::vector<FileEntry>& out, std::set<std::string>& seen, const SessionInfo& session,
                         const std::string& path) const;
    std::vector<FileEntry> EnumerateSessionRoot(const SessionInfo& session) const;
    std::string BuildListResponse(const std::string& cwd, const std::vector<FileEntry>& entries) const;

    void HandleLogin(SOCKET sock, const NetPacket& req);
    void HandleList(SOCKET sock, const NetPacket& req);
    void HandleMakeDir(SOCKET sock, const NetPacket& req);
    void HandleRemove(SOCKET sock, const NetPacket& req);
    void HandleRename(SOCKET sock, const NetPacket& req);
    void HandleLogout(const NetPacket& req);
    void HandleCommand(SOCKET sock, const NetPacket& req, bool& removeSocket, bool& keepSocketOpen);

    void SpawnUploadWorker(SOCKET sock, NetPacket req);
    void SpawnDownloadWorker(SOCKET sock, NetPacket req);
    void UploadWorker(SOCKET sock, const NetPacket& req);
    void DownloadWorker(SOCKET sock, const NetPacket& req);
    void RegisterTransferSocket(SOCKET sock);
    void UnregisterTransferSocket(SOCKET sock);
    void Loop();

    std::filesystem::path dataDir_;
    std::filesystem::path rootDir_;
    std::filesystem::path usersDbFile_;
    std::filesystem::path legacyUsersFile_;
    UserStore userStore_;

    mutable std::mutex mu_;
    mutable std::mutex transferMu_;
    std::unordered_map<std::string, UserRecord> users_;
    std::unordered_map<std::uint32_t, SessionInfo> sessions_;
    std::vector<SOCKET> clientSockets_;
    std::vector<SOCKET> transferSockets_;
    std::string userStoreError_;
    std::vector<TransferSnapshot> transfers_;

    std::atomic<bool> running_{false};
    std::atomic<int> activeClients_{0};
    std::atomic<int> activeTransfers_{0};
    std::atomic<std::uint32_t> nextSession_{1};
    std::atomic<std::uint32_t> nextTransfer_{1};

    SOCKET listenSock_ = INVALID_SOCKET;
    std::thread loopThread_;
    int port_ = kDefaultPort;
};

}  // namespace fds::serverapp
