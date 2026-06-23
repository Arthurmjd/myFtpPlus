#pragma once

#include "protocol.hpp"

#include <memory>

namespace fds::clientapp {

std::string JoinRemotePath(const std::string& base, const std::string& name);

struct Favorite {
    std::string host;
    int port = kDefaultPort;
    std::string user;
    bool anonymous = false;
};

class FavoriteStore {
public:
    explicit FavoriteStore(std::filesystem::path path);

    void Load();
    bool Add(const Favorite& favorite);

    const std::vector<Favorite>& Items() const;
    const Favorite* Get(std::size_t index) const;
    std::vector<std::wstring> Labels() const;

private:
    std::string ReadTextFile() const;
    void Save() const;

    std::filesystem::path path_;
    std::vector<Favorite> items_;
};

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

enum class TaskState {
    Queued,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled,
};

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

std::wstring TaskStateText(TaskState state);

class TaskManager {
public:
    std::shared_ptr<TransferTask> Add(bool upload, const std::wstring& local, const std::string& remote,
                                      const ConnectionInfo& connection);
    std::shared_ptr<TransferTask> GetAt(int index) const;
    const std::vector<std::shared_ptr<TransferTask>>& Items() const;
    bool HasRunningTasks() const;

    void Pause(const std::shared_ptr<TransferTask>& task) const;
    void Resume(const std::shared_ptr<TransferTask>& task, const ConnectionInfo& connection);
    void Cancel(const std::shared_ptr<TransferTask>& task) const;

private:
    void Start(const std::shared_ptr<TransferTask>& task, const ConnectionInfo& connection) const;

    int nextId_ = 1;
    std::vector<std::shared_ptr<TransferTask>> items_;
};

}  // namespace fds::clientapp
