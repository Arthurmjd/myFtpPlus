#include "state.hpp"

#include <fstream>

namespace fds::clientapp {

namespace {

void SetTaskInfo(const std::shared_ptr<TransferTask>& task, TaskState state, const std::wstring& info) {
    task->state = state;
    task->speed = 0.0;
    std::lock_guard lock(task->infoMu);
    task->info = info;
}

}  // namespace

std::string JoinRemotePath(const std::string& base, const std::string& name) {
    if (base == "/") {
        return NormalizeVirtualPath("/" + name);
    }
    return NormalizeVirtualPath(base + "/" + name);
}

FavoriteStore::FavoriteStore(std::filesystem::path path) : path_(std::move(path)) {}

void FavoriteStore::Load() {
    items_.clear();
    for (const auto& row : ParseLines(ReadTextFile())) {
        if (row.size() < 4) {
            continue;
        }
        Favorite favorite;
        favorite.host = row[0];
        favorite.port = std::atoi(row[1].c_str());
        favorite.user = row[2];
        favorite.anonymous = row[3] == "1";
        items_.push_back(std::move(favorite));
    }
}

bool FavoriteStore::Add(const Favorite& favorite) {
    for (const auto& item : items_) {
        if (item.host == favorite.host && item.port == favorite.port && item.user == favorite.user &&
            item.anonymous == favorite.anonymous) {
            return false;
        }
    }
    items_.push_back(favorite);
    Save();
    return true;
}

const std::vector<Favorite>& FavoriteStore::Items() const {
    return items_;
}

const Favorite* FavoriteStore::Get(std::size_t index) const {
    return index < items_.size() ? &items_[index] : nullptr;
}

std::vector<std::wstring> FavoriteStore::Labels() const {
    std::vector<std::wstring> labels;
    labels.reserve(items_.size());
    for (const auto& favorite : items_) {
        const std::string suffix = favorite.anonymous ? "匿名登录" : favorite.user;
        labels.push_back(Utf8ToWide(favorite.host + ":" + std::to_string(favorite.port) + " / " + suffix));
    }
    return labels;
}

std::string FavoriteStore::ReadTextFile() const {
    std::ifstream in(path_);
    if (!in) {
        return "";
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void FavoriteStore::Save() const {
    std::filesystem::create_directories(path_.parent_path());
    std::ofstream out(path_, std::ios::trunc);
    for (const auto& favorite : items_) {
        out << MakeLine({
            favorite.host,
            std::to_string(favorite.port),
            favorite.user,
            favorite.anonymous ? "1" : "0",
        });
    }
}

void NavigationHistory::Reset() {
    items_.clear();
    index_ = -1;
}

void NavigationHistory::Push(const std::string& path) {
    if (index_ + 1 < static_cast<int>(items_.size())) {
        items_.erase(items_.begin() + index_ + 1, items_.end());
    }
    items_.push_back(path);
    index_ = static_cast<int>(items_.size()) - 1;
}

std::optional<std::string> NavigationHistory::Back() {
    if (index_ <= 0) {
        return std::nullopt;
    }
    --index_;
    return items_[index_];
}

std::optional<std::string> NavigationHistory::Forward() {
    if (index_ + 1 >= static_cast<int>(items_.size())) {
        return std::nullopt;
    }
    ++index_;
    return items_[index_];
}

std::wstring TaskStateText(TaskState state) {
    switch (state) {
        case TaskState::Queued: return L"等待中";
        case TaskState::Running: return L"传输中";
        case TaskState::Paused: return L"已暂停";
        case TaskState::Completed: return L"已完成";
        case TaskState::Failed: return L"失败";
        case TaskState::Cancelled: return L"已取消";
    }
    return L"未知";
}

std::shared_ptr<TransferTask> TaskManager::Add(bool upload, const std::wstring& local, const std::string& remote,
                                               const ConnectionInfo& connection) {
    auto task = std::make_shared<TransferTask>();
    task->id = nextId_++;
    task->upload = upload;
    task->local = local;
    task->remote = remote;
    items_.push_back(task);
    Start(task, connection);
    return task;
}

std::shared_ptr<TransferTask> TaskManager::GetAt(int index) const {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return nullptr;
    }
    return items_[index];
}

const std::vector<std::shared_ptr<TransferTask>>& TaskManager::Items() const {
    return items_;
}

void TaskManager::Pause(const std::shared_ptr<TransferTask>& task) const {
    if (task) {
        task->pauseWanted = true;
    }
}

void TaskManager::Resume(const std::shared_ptr<TransferTask>& task, const ConnectionInfo& connection) {
    if (!task) {
        return;
    }
    const auto state = task->state.load();
    if (state == TaskState::Paused || state == TaskState::Failed) {
        Start(task, connection);
    }
}

void TaskManager::Cancel(const std::shared_ptr<TransferTask>& task) const {
    if (task) {
        task->cancelWanted = true;
    }
}

void TaskManager::Start(const std::shared_ptr<TransferTask>& task, const ConnectionInfo& connection) const {
    task->state = TaskState::Running;
    task->pauseWanted = false;
    task->cancelWanted = false;

    std::thread([task, connection] {
        std::uint64_t lastDone = task->done.load();
        auto lastTick = std::chrono::steady_clock::now();
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

            if (task->cancelWanted.load()) {
                return FlowControl::Cancel;
            }
            if (task->pauseWanted.load()) {
                return FlowControl::Pause;
            }
            return FlowControl::Continue;
        };

        const TransferResult result =
            task->upload ? UploadFileSync(connection, task->local, task->remote, progress)
                         : DownloadFileSync(connection, task->remote, task->local, progress);

        switch (result.outcome) {
            case TransferOutcome::Success:
                SetTaskInfo(task, TaskState::Completed, L"哈希校验通过");
                return;
            case TransferOutcome::Paused:
                SetTaskInfo(task, TaskState::Paused, L"已暂停，可继续传输");
                return;
            case TransferOutcome::Cancelled:
                SetTaskInfo(task, TaskState::Cancelled, L"任务已取消");
                return;
            case TransferOutcome::Failed:
                SetTaskInfo(task, TaskState::Failed, Utf8ToWide(result.message));
                return;
        }
    }).detach();
}

}  // namespace fds::clientapp
