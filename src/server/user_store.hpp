#pragma once

#include "shared/common.hpp"

namespace fds::serverapp {

// 用户存储层：负责 SQLite 读写，并兼容旧版 TSV 数据。
class UserStore {
public:
    UserStore(std::filesystem::path dbPath, std::filesystem::path legacyPath);

    // 先从 SQLite 读取；为空时再导入旧数据或写入种子用户。
    bool Load(std::vector<UserRecord>& users, const std::vector<UserRecord>& seedUsers, std::string& error) const;
    // 把当前用户列表完整覆盖保存到 SQLite。
    bool Save(const std::vector<UserRecord>& users, std::string& error) const;

private:
    std::filesystem::path dbPath_;
    std::filesystem::path legacyPath_;
};

}  // namespace fds::serverapp
