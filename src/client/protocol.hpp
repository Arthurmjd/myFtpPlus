#pragma once

#include "shared/common.hpp"

#include <filesystem>
#include <functional>

namespace fds::clientapp {

// 传输线程通过它告诉调用方：继续、暂停还是取消。
enum class FlowControl {
    Continue,
    Pause,
    Cancel,
};

// 新建上传/下载连接时需要的连接信息。
struct ConnectionInfo {
    std::string host;
    int port = kDefaultPort;
    std::uint32_t session = 0;
};

// 一次上传/下载结束后的统一结果状态。
enum class TransferOutcome {
    Success,
    Paused,
    Cancelled,
    Failed,
};

// 同步上传/下载返回给任务管理器的结果对象。
struct TransferResult {
    TransferOutcome outcome = TransferOutcome::Failed;
    std::string message;
};

// 进度回调：参数分别是已完成字节数和总字节数。
using ProgressCallback = std::function<FlowControl(std::uint64_t done, std::uint64_t total)>;

// 控制连接客户端：负责登录、列目录、创建目录、删除和重命名。
class CommandClient {
public:
    ~CommandClient();

    // 建立控制连接并完成登录。
    bool Connect(const std::string& host, int port, const std::string& user, const std::string& password,
                 bool anonymous, std::string& error);
    // 主动注销并关闭控制连接。
    void Disconnect();

    // 当前连接状态和登录信息。
    bool Connected() const;
    std::uint32_t Session() const;
    const std::string& Host() const;
    int Port() const;
    const std::string& Home() const;
    const std::string& Username() const;
    bool IsAdmin() const;

    // 目录和文件管理命令。
    bool List(const std::string& path, std::string& cwd, std::vector<FileEntry>& items, std::string& error);
    bool MakeDir(const std::string& path, std::string& error);
    bool Remove(const std::string& path, std::string& error);
    bool Rename(const std::string& path, const std::string& newName, std::string& error);

private:
    // 统一的一问一答式控制命令调用入口。
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

// 独立传输连接上的同步上传/下载函数，供任务线程直接调用。
TransferResult UploadFileSync(const ConnectionInfo& connection, const std::filesystem::path& localPath,
                              const std::string& remotePath, ProgressCallback onProgress);
TransferResult DownloadFileSync(const ConnectionInfo& connection, const std::string& remotePath,
                                const std::filesystem::path& localPath, ProgressCallback onProgress);

}  // namespace fds::clientapp
