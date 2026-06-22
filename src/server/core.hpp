#pragma once

#include "shared/common.hpp"

#include <unordered_map>

namespace fds::serverapp {

class ServerCore {
public:
    ServerCore();
    ~ServerCore();

    bool Start(int port, std::string& error);
    void Stop();

    bool IsRunning() const;
    std::wstring StatusText() const;
    std::vector<UserRecord> SnapshotUsers();
    bool UpsertUser(const UserRecord& input, const std::string& plainPassword, std::string& error);
    bool DeleteUser(const std::string& username, std::string& error);
    std::wstring ReadLogs() const;

private:
    struct ResolvedPath {
        std::filesystem::path real;
        std::string virtualPath;
    };

    void EnsureLayout();
    void EnsureUserDirsLocked(const UserRecord& user);
    void LoadUsersLocked();
    void SaveUsersLocked();
    void AppendLog(const std::string& user, const std::string& action, const std::string& detail) const;
    std::optional<SessionInfo> FindSession(std::uint32_t sessionId);
    std::optional<ResolvedPath> ResolvePath(const SessionInfo& session, const std::string& rawPath, std::uint32_t bit,
                                            bool allowMissing = false);

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
    void Loop();

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
};

}  // namespace fds::serverapp
