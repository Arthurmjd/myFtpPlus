#include "user_store.hpp"

namespace fds::serverapp {

namespace {

struct sqlite3;
struct sqlite3_stmt;

constexpr int SQLITE_OK = 0;
constexpr int SQLITE_ROW = 100;
constexpr int SQLITE_DONE = 101;
constexpr int SQLITE_OPEN_READWRITE = 0x00000002;
constexpr int SQLITE_OPEN_CREATE = 0x00000004;

// SQLite 动态加载器：运行时装载 DLL，并缓存函数指针。
class SqliteApi {
public:
    // 首次使用时加载 SQLite 运行库和所需 API。
    bool EnsureLoaded(std::string& error) {
        if (loaded_) {
            return true;
        }

        module_ = LoadLibraryW(L"winsqlite3.dll");
        if (!module_) {
            module_ = LoadLibraryW(L"sqlite3.dll");
        }
        if (!module_) {
            error = "无法加载 SQLite 运行库（winsqlite3.dll 或 sqlite3.dll）";
            return false;
        }

        if (!Load(openV2_, "sqlite3_open_v2", error) || !Load(close_, "sqlite3_close", error) ||
            !Load(exec_, "sqlite3_exec", error) || !Load(prepareV2_, "sqlite3_prepare_v2", error) ||
            !Load(step_, "sqlite3_step", error) || !Load(finalize_, "sqlite3_finalize", error) ||
            !Load(bindText_, "sqlite3_bind_text", error) || !Load(bindInt_, "sqlite3_bind_int", error) ||
            !Load(columnText_, "sqlite3_column_text", error) || !Load(columnInt_, "sqlite3_column_int", error) ||
            !Load(errmsg_, "sqlite3_errmsg", error)) {
            FreeLibrary(module_);
            module_ = nullptr;
            return false;
        }

        loaded_ = true;
        return true;
    }

    // 进程结束时释放 DLL 模块句柄。
    ~SqliteApi() {
        if (module_) {
            FreeLibrary(module_);
        }
    }

    using OpenV2Fn = int(__cdecl*)(const char*, sqlite3**, int, const char*);
    using CloseFn = int(__cdecl*)(sqlite3*);
    using ExecFn = int(__cdecl*)(sqlite3*, const char*, int(__cdecl*)(void*, int, char**, char**), void*, char**);
    using PrepareV2Fn = int(__cdecl*)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
    using StepFn = int(__cdecl*)(sqlite3_stmt*);
    using FinalizeFn = int(__cdecl*)(sqlite3_stmt*);
    using BindTextFn = int(__cdecl*)(sqlite3_stmt*, int, const char*, int, void(__cdecl*)(void*));
    using BindIntFn = int(__cdecl*)(sqlite3_stmt*, int, int);
    using ColumnTextFn = const unsigned char*(__cdecl*)(sqlite3_stmt*, int);
    using ColumnIntFn = int(__cdecl*)(sqlite3_stmt*, int);
    using ErrmsgFn = const char*(__cdecl*)(sqlite3*);

    OpenV2Fn openV2_{};
    CloseFn close_{};
    ExecFn exec_{};
    PrepareV2Fn prepareV2_{};
    StepFn step_{};
    FinalizeFn finalize_{};
    BindTextFn bindText_{};
    BindIntFn bindInt_{};
    ColumnTextFn columnText_{};
    ColumnIntFn columnInt_{};
    ErrmsgFn errmsg_{};

private:
    template <typename T>
    // 从 DLL 中取出一个 API 函数地址。
    bool Load(T& target, const char* name, std::string& error) {
        target = reinterpret_cast<T>(GetProcAddress(module_, name));
        if (target) {
            return true;
        }
        error = std::string("SQLite API 缺失: ") + name;
        return false;
    }

    HMODULE module_ = nullptr;
    bool loaded_ = false;
};

// 返回整个进程共享的一份 SQLite API 单例。
SqliteApi& GetSqliteApi() {
    static SqliteApi api;
    return api;
}

// SQLite 连接封装，负责打开数据库、执行 SQL 和读取错误信息。
class SqliteDb {
public:
    explicit SqliteDb(SqliteApi& api) : api_(api) {}

    // 析构时自动关闭数据库连接。
    ~SqliteDb() {
        if (db_) {
            api_.close_(db_);
        }
    }

    // 打开数据库文件；必要时自动创建父目录。
    bool Open(const std::filesystem::path& path, std::string& error) {
        std::filesystem::create_directories(path.parent_path());
        const std::string utf8Path = WideToUtf8(path.wstring());
        if (api_.openV2_(utf8Path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
            error = LastError();
            return false;
        }
        return true;
    }

    // 执行不需要结果集的 SQL。
    bool Exec(const char* sql, std::string& error) const {
        if (api_.exec_(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            error = LastError();
            return false;
        }
        return true;
    }

    // 预编译一条 SQL 语句。
    bool Prepare(const char* sql, sqlite3_stmt** stmt, std::string& error) const {
        if (api_.prepareV2_(db_, sql, -1, stmt, nullptr) != SQLITE_OK) {
            error = LastError();
            return false;
        }
        return true;
    }

    // 读取最近一次数据库错误消息。
    std::string LastError() const {
        if (!db_) {
            return "SQLite 数据库未打开";
        }
        if (const char* message = api_.errmsg_(db_)) {
            return message;
        }
        return "SQLite 错误";
    }

    // 暴露底层 API，供语句封装继续调用。
    SqliteApi& Api() const {
        return api_;
    }

private:
    SqliteApi& api_;
    sqlite3* db_ = nullptr;
};

// SQLite 预编译语句封装，负责绑定参数、取列值和自动 finalize。
class SqliteStatement {
public:
    SqliteStatement(const SqliteDb& db, sqlite3_stmt* stmt) : db_(db), stmt_(stmt) {}

    // 析构时自动释放 statement。
    ~SqliteStatement() {
        if (stmt_) {
            db_.Api().finalize_(stmt_);
        }
    }

    // 绑定文本参数。
    bool BindText(int index, const std::string& value, std::string& error) const {
        if (db_.Api().bindText_(stmt_, index, value.c_str(), static_cast<int>(value.size()), nullptr) != SQLITE_OK) {
            error = db_.LastError();
            return false;
        }
        return true;
    }

    // 绑定整型参数。
    bool BindInt(int index, int value, std::string& error) const {
        if (db_.Api().bindInt_(stmt_, index, value) != SQLITE_OK) {
            error = db_.LastError();
            return false;
        }
        return true;
    }

    // 向前执行一步，可能得到一行结果，也可能执行完成。
    int Step() const {
        return db_.Api().step_(stmt_);
    }

    // 读取文本列。
    std::string ColumnText(int index) const {
        const auto* value = db_.Api().columnText_(stmt_, index);
        return value ? reinterpret_cast<const char*>(value) : "";
    }

    // 读取整型列。
    int ColumnInt(int index) const {
        return db_.Api().columnInt_(stmt_, index);
    }

private:
    const SqliteDb& db_;
    sqlite3_stmt* stmt_ = nullptr;
};

// 确保用户表结构存在。
bool EnsureSchema(const SqliteDb& db, std::string& error) {
    return db.Exec(
        "CREATE TABLE IF NOT EXISTS users ("
        "username TEXT PRIMARY KEY,"
        "password_hash TEXT NOT NULL,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "home TEXT NOT NULL,"
        "admin INTEGER NOT NULL DEFAULT 0,"
        "rule_spec TEXT NOT NULL"
        ");",
        error);
}

// 用事务把内存中的用户列表完整覆盖保存到数据库。
bool SaveUsersToDb(const SqliteDb& db, const std::vector<UserRecord>& users, std::string& error) {
    if (!db.Exec("BEGIN IMMEDIATE TRANSACTION;", error)) {
        return false;
    }

    if (!db.Exec("DELETE FROM users;", error)) {
        std::string ignored;
        db.Exec("ROLLBACK;", ignored);
        return false;
    }

    for (const auto& user : users) {
        const auto normalizedHome = NormalizeVirtualPath(user.home);
        sqlite3_stmt* rawStmt = nullptr;
        if (!db.Prepare(
                "INSERT INTO users (username, password_hash, enabled, home, admin, rule_spec) "
                "VALUES (?, ?, ?, ?, ?, ?);",
                &rawStmt, error)) {
            std::string ignored;
            db.Exec("ROLLBACK;", ignored);
            return false;
        }

        SqliteStatement stmt(db, rawStmt);
        if (!stmt.BindText(1, user.username, error) || !stmt.BindText(2, user.passwordHash, error) ||
            !stmt.BindInt(3, user.enabled ? 1 : 0, error) ||
            !stmt.BindText(4, normalizedHome, error) ||
            !stmt.BindInt(5, user.admin ? 1 : 0, error) || !stmt.BindText(6, user.ruleSpec, error)) {
            std::string ignored;
            db.Exec("ROLLBACK;", ignored);
            return false;
        }

        if (stmt.Step() != SQLITE_DONE) {
            error = db.LastError();
            std::string ignored;
            db.Exec("ROLLBACK;", ignored);
            return false;
        }
    }

    if (!db.Exec("COMMIT;", error)) {
        std::string ignored;
        db.Exec("ROLLBACK;", ignored);
        return false;
    }
    return true;
}

// 从数据库读取全部用户，并转换为内存结构。
bool LoadUsersFromDb(const SqliteDb& db, std::vector<UserRecord>& users, std::string& error) {
    sqlite3_stmt* rawStmt = nullptr;
    if (!db.Prepare(
            "SELECT username, password_hash, enabled, home, admin, rule_spec "
            "FROM users ORDER BY username COLLATE NOCASE;",
            &rawStmt, error)) {
        return false;
    }

    SqliteStatement stmt(db, rawStmt);
    users.clear();
    while (true) {
        const int rc = stmt.Step();
        if (rc == SQLITE_DONE) {
            return true;
        }
        if (rc != SQLITE_ROW) {
            error = db.LastError();
            return false;
        }

        UserRecord user;
        user.username = stmt.ColumnText(0);
        user.passwordHash = stmt.ColumnText(1);
        user.enabled = stmt.ColumnInt(2) != 0;
        user.home = NormalizeVirtualPath(stmt.ColumnText(3));
        user.admin = stmt.ColumnInt(4) != 0;
        user.ruleSpec = stmt.ColumnText(5);
        user.rules = ParseRuleSpec(user.ruleSpec);
        if (!user.username.empty() && !user.passwordHash.empty() && !user.rules.empty()) {
            users.push_back(std::move(user));
        }
    }
}

}  // namespace

// 保存数据库文件和旧版 TSV 文件路径。
UserStore::UserStore(std::filesystem::path dbPath, std::filesystem::path legacyPath)
    : dbPath_(std::move(dbPath)), legacyPath_(std::move(legacyPath)) {}

// 从 SQLite 读取用户；若数据库为空，则尝试导入旧版 TSV 或种子用户。
bool UserStore::Load(std::vector<UserRecord>& users, const std::vector<UserRecord>& seedUsers, std::string& error) const {
    auto& api = GetSqliteApi();
    if (!api.EnsureLoaded(error)) {
        return false;
    }

    SqliteDb db(api);
    if (!db.Open(dbPath_, error) || !EnsureSchema(db, error)) {
        return false;
    }

    if (!LoadUsersFromDb(db, users, error)) {
        return false;
    }
    if (!users.empty()) {
        return true;
    }

    auto initialUsers = LoadUsers(legacyPath_);
    if (initialUsers.empty()) {
        initialUsers = seedUsers;
    }
    if (!SaveUsersToDb(db, initialUsers, error)) {
        return false;
    }

    users = std::move(initialUsers);
    return true;
}

// 把用户列表写回 SQLite。
bool UserStore::Save(const std::vector<UserRecord>& users, std::string& error) const {
    auto& api = GetSqliteApi();
    if (!api.EnsureLoaded(error)) {
        return false;
    }

    SqliteDb db(api);
    if (!db.Open(dbPath_, error) || !EnsureSchema(db, error)) {
        return false;
    }
    return SaveUsersToDb(db, users, error);
}

}  // namespace fds::serverapp
