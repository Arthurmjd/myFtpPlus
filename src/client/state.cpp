#include "state.hpp"

#include <fstream>

namespace fds::clientapp {

namespace {

// 同时更新任务状态和说明文字，适合任务结束时调用。
void SetTaskInfo(const std::shared_ptr<TransferTask>& task, TaskState state, const std::wstring& info) {
    task->state = state;
    task->speed = 0.0;
    std::lock_guard lock(task->infoMu);
    task->info = info;
}

// 只更新任务说明，不改变任务状态。
void SetTaskMessage(const std::shared_ptr<TransferTask>& task, const std::wstring& info) {
    std::lock_guard lock(task->infoMu);
    task->info = info;
}

// 生成任务刚启动时显示的说明文字，区分全新传输和断点续传。
std::wstring StartInfoText(bool upload, std::uint64_t offset) {
    if (offset > 0) {
        return L"断点续传，从 " + Utf8ToWide(FormatBytes(offset)) + L" 继续";
    }
    return upload ? L"从头开始上传" : L"从头开始下载";
}

// 生成暂停后的说明文字，提示用户可从哪个偏移继续。
std::wstring PausedInfoText(std::uint64_t done) {
    if (done > 0) {
        return L"已暂停，可从 " + Utf8ToWide(FormatBytes(done)) + L" 继续";
    }
    return L"已暂停，可继续传输";
}

// 生成成功完成后的说明文字。
std::wstring CompletedInfoText(std::uint64_t resumeFrom) {
    return resumeFrom > 0 ? L"断点续传完成，校验通过" : L"传输完成，校验通过";
}

// 生成失败说明；如果已有部分数据，则附带“可继续续传”提示。
std::wstring FailedInfoText(const std::shared_ptr<TransferTask>& task, const std::string& message) {
    std::wstring text = Utf8ToWide(message);
    if (task->done.load() > 0) {
        text += L"；可继续续传";
    }
    return text;
}

}  // namespace

// 拼接远程虚拟路径，统一处理根目录场景。
std::string JoinRemotePath(const std::string& base, const std::string& name) {
    if (base == "/") {
        return NormalizeVirtualPath("/" + name);
    }
    return NormalizeVirtualPath(base + "/" + name);
}

// 记录常用连接保存文件的路径。
FavoriteStore::FavoriteStore(std::filesystem::path path) : path_(std::move(path)) {}

// 从本地 TSV 文件读取常用连接。
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

// 去重后追加一条常用连接，并立即写回文件。
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

// 返回常用连接列表本体。
const std::vector<Favorite>& FavoriteStore::Items() const {
    return items_;
}

// 按索引获取一条常用连接。
const Favorite* FavoriteStore::Get(std::size_t index) const {
    return index < items_.size() ? &items_[index] : nullptr;
}

// 生成下拉框中展示的可读标签。
std::vector<std::wstring> FavoriteStore::Labels() const {
    std::vector<std::wstring> labels;
    labels.reserve(items_.size());
    for (const auto& favorite : items_) {
        const std::string suffix = favorite.anonymous ? "匿名登录" : favorite.user;
        labels.push_back(Utf8ToWide(favorite.host + ":" + std::to_string(favorite.port) + " / " + suffix));
    }
    return labels;
}

// 读取整个常用连接文本文件。
std::string FavoriteStore::ReadTextFile() const {
    std::ifstream in(path_);
    if (!in) {
        return "";
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// 把常用连接列表完整写回磁盘。
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

// 清空目录浏览历史。
void NavigationHistory::Reset() {
    items_.clear();
    index_ = -1;
}

// 把新路径压入历史，同时截断当前位置之后的“前进”记录。
void NavigationHistory::Push(const std::string& path) {
    if (index_ + 1 < static_cast<int>(items_.size())) {
        items_.erase(items_.begin() + index_ + 1, items_.end());
    }
    items_.push_back(path);
    index_ = static_cast<int>(items_.size()) - 1;
}

// 返回上一个浏览路径。
std::optional<std::string> NavigationHistory::Back() {
    if (index_ <= 0) {
        return std::nullopt;
    }
    --index_;
    return items_[index_];
}

// 返回前进路径。
std::optional<std::string> NavigationHistory::Forward() {
    if (index_ + 1 >= static_cast<int>(items_.size())) {
        return std::nullopt;
    }
    ++index_;
    return items_[index_];
}

// 把任务枚举状态转换成界面展示文字。
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

// 创建新任务并立即启动后台线程。
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

// 按列表索引取任务，供界面当前选中行使用。
std::shared_ptr<TransferTask> TaskManager::GetAt(int index) const {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return nullptr;
    }
    return items_[index];
}

// 返回任务列表本体，供界面刷新使用。
const std::vector<std::shared_ptr<TransferTask>>& TaskManager::Items() const {
    return items_;
}

// 判断是否还有未结束任务，用于限制退出登录。
bool TaskManager::HasRunningTasks() const {
    for (const auto& item : items_) {
        const auto state = item->state.load();
        if (state == TaskState::Queued || state == TaskState::Running) {
            return true;
        }
    }
    return false;
}

// 请求暂停：真正暂停会在传输循环下次检查进度时生效。
void TaskManager::Pause(const std::shared_ptr<TransferTask>& task) const {
    if (task) {
        task->pauseWanted = true;
    }
}

// 对已暂停或失败任务重新建连，并依赖断点续传继续执行。
void TaskManager::Resume(const std::shared_ptr<TransferTask>& task, const ConnectionInfo& connection) {
    if (!task) {
        return;
    }
    const auto state = task->state.load();
    if (state == TaskState::Paused || state == TaskState::Failed) {
        Start(task, connection);
    }
}

// 请求取消：真正停止会在传输循环下次检查进度时生效。
void TaskManager::Cancel(const std::shared_ptr<TransferTask>& task) const {
    if (task) {
        task->cancelWanted = true;
    }
}

// 启动真正的传输线程，并把上传/下载结果回填到任务对象中。
void TaskManager::Start(const std::shared_ptr<TransferTask>& task, const ConnectionInfo& connection) const {
    task->state = TaskState::Running;
    task->pauseWanted = false;
    task->cancelWanted = false;
    task->speed = 0.0;

    std::thread([task, connection] {
        std::uint64_t lastDone = task->done.load();
        auto lastTick = std::chrono::steady_clock::now();
        bool startInfoSet = false;

        auto progress = [&](std::uint64_t done, std::uint64_t total) {
            task->done = done;
            task->total = total;
            if (!startInfoSet) {
                task->resumeFrom = done;
                SetTaskMessage(task, StartInfoText(task->upload, done));
                startInfoSet = true;
            }

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
                SetTaskInfo(task, TaskState::Completed, CompletedInfoText(task->resumeFrom.load()));
                return;
            case TransferOutcome::Paused:
                SetTaskInfo(task, TaskState::Paused, PausedInfoText(task->done.load()));
                return;
            case TransferOutcome::Cancelled:
                SetTaskInfo(task, TaskState::Cancelled, L"任务已取消");
                return;
            case TransferOutcome::Failed:
                SetTaskInfo(task, TaskState::Failed, FailedInfoText(task, result.message));
                return;
        }
    }).detach();
}

}  // namespace fds::clientapp
