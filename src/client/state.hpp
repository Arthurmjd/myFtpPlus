#pragma once

#include "protocol.hpp"

#include <memory>

namespace fds::clientapp {

// 拼接远程虚拟路径，并保证返回值仍然是规范化路径。
std::string JoinRemotePath(const std::string& base, const std::string& name);

// 一条常用连接记录。
struct Favorite {
    std::string host;
    int port = kDefaultPort;
    std::string user;
    bool anonymous = false;
};

// 本地常用连接存储。
class FavoriteStore {
public:
    explicit FavoriteStore(std::filesystem::path path);

    // 从本地 TSV 读取连接记录，或把新记录追加并写回。
    void Load();
    bool Add(const Favorite& favorite);

    // 供界面读取的数据接口。
    const std::vector<Favorite>& Items() const;
    const Favorite* Get(std::size_t index) const;
    std::vector<std::wstring> Labels() const;

private:
    // 纯文本读写辅助函数。
    std::string ReadTextFile() const;
    void Save() const;

    std::filesystem::path path_;
    std::vector<Favorite> items_;
};

// 目录浏览历史，支持前进与后退。
class NavigationHistory {
public:
    void Reset();
    void Push(const std::string& path);
    std::optional<std::string> Back();
    std::optional<std::string> Forward();

private:
    std::vector<std::string> items_;
    int index_ = -1;
};

// 客户端任务列表展示用的状态。
enum class TaskState {
    Queued,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled,
};

// 单个上传/下载任务的运行时状态。
struct TransferTask {
    int id = 0;
    bool upload = false;
    std::wstring local;
    std::string remote;
    std::atomic<std::uint64_t> resumeFrom{0};
    std::atomic<std::uint64_t> done{0};
    std::atomic<std::uint64_t> total{0};
    std::atomic<double> speed{0.0};
    std::atomic<TaskState> state{TaskState::Queued};
    std::atomic<bool> pauseWanted{false};
    std::atomic<bool> cancelWanted{false};
    std::mutex infoMu;
    std::wstring info = L"等待中";
};

// 把任务状态枚举转换为界面上的说明文字。
std::wstring TaskStateText(TaskState state);

// 客户端任务管理器：创建任务、启动线程、控制暂停/继续/取消。
class TaskManager {
public:
    std::shared_ptr<TransferTask> Add(bool upload, const std::wstring& local, const std::string& remote,
                                      const ConnectionInfo& connection);
    std::shared_ptr<TransferTask> GetAt(int index) const;
    const std::vector<std::shared_ptr<TransferTask>>& Items() const;
    // 用于阻止用户在仍有运行任务时直接退出登录。
    bool HasRunningTasks() const;

    void Pause(const std::shared_ptr<TransferTask>& task) const;
    void Resume(const std::shared_ptr<TransferTask>& task, const ConnectionInfo& connection);
    void Cancel(const std::shared_ptr<TransferTask>& task) const;

private:
    // 真正启动后台线程并执行上传/下载逻辑。
    void Start(const std::shared_ptr<TransferTask>& task, const ConnectionInfo& connection) const;

    int nextId_ = 1;
    std::vector<std::shared_ptr<TransferTask>> items_;
};

}  // namespace fds::clientapp
