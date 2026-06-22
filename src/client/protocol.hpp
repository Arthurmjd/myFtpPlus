#pragma once

#include "shared/common.hpp"

#include <filesystem>
#include <functional>

namespace fds::clientapp {

enum class FlowControl {
    Continue,
    Pause,
    Cancel,
};

struct ConnectionInfo {
    std::string host;
    int port = kDefaultPort;
    std::uint32_t session = 0;
};

enum class TransferOutcome {
    Success,
    Paused,
    Cancelled,
    Failed,
};

struct TransferResult {
    TransferOutcome outcome = TransferOutcome::Failed;
    std::string message;
};

using ProgressCallback = std::function<FlowControl(std::uint64_t done, std::uint64_t total)>;

class CommandClient {
public:
    ~CommandClient();

    bool Connect(const std::string& host, int port, const std::string& user, const std::string& password,
                 bool anonymous, std::string& error);
    void Disconnect();

    bool Connected() const;
    std::uint32_t Session() const;
    const std::string& Host() const;
    int Port() const;
    const std::string& Home() const;
    const std::string& Username() const;
    bool IsAdmin() const;

    bool List(const std::string& path, std::string& cwd, std::vector<FileEntry>& items, std::string& error);
    bool MakeDir(const std::string& path, std::string& error);
    bool Remove(const std::string& path, std::string& error);
    bool Rename(const std::string& path, const std::string& newName, std::string& error);

private:
    bool Call(Cmd cmd, const std::map<std::string, std::string>& request, Cmd expected, std::string& body,
              std::string& error);

    SOCKET sock_ = INVALID_SOCKET;
    std::uint32_t session_ = 0;
    std::uint32_t nextSeq_ = 1;
    std::string host_;
    std::string home_;
    std::string user_;
    int port_ = kDefaultPort;
    bool admin_ = false;
};

TransferResult UploadFileSync(const ConnectionInfo& connection, const std::filesystem::path& localPath,
                              const std::string& remotePath, ProgressCallback onProgress);
TransferResult DownloadFileSync(const ConnectionInfo& connection, const std::string& remotePath,
                                const std::filesystem::path& localPath, ProgressCallback onProgress);

}  // namespace fds::clientapp
