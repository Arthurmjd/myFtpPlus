#pragma once

#include "shared/common.hpp"

namespace fds::serverapp {

class UserStore {
public:
    UserStore(std::filesystem::path dbPath, std::filesystem::path legacyPath);

    bool Load(std::vector<UserRecord>& users, const std::vector<UserRecord>& seedUsers, std::string& error) const;
    bool Save(const std::vector<UserRecord>& users, std::string& error) const;

private:
    std::filesystem::path dbPath_;
    std::filesystem::path legacyPath_;
};

}  // namespace fds::serverapp
