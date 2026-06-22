#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fds {

using u64 = std::uint64_t;

constexpr std::uint32_t kMagic = 0x31445346;  // "FDS1"
constexpr std::uint16_t kVersion = 1;
constexpr int kDefaultPort = 9527;
constexpr std::size_t kChunkSize = 64 * 1024;

enum class Cmd : std::uint16_t {
    Login = 1,
    LoginOk,
    Error,
    Ok,
    List,
    ListResult,
    MakeDir,
    Remove,
    Rename,
    UploadBegin,
    UploadReady,
    UploadData,
    UploadEnd,
    UploadDone,
    DownloadBegin,
    DownloadMeta,
    DownloadData,
    DownloadDone,
    Logout,
    Ping
};

enum PermissionBits : std::uint32_t {
    PermRead = 1 << 0,
    PermWrite = 1 << 1,
    PermDelete = 1 << 2,
    PermRename = 1 << 3,
};

#pragma pack(push, 1)
struct PacketHeader {
    // 固定长度头部用于解决 TCP 粘包/拆包。
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    std::uint16_t cmd = 0;
    std::uint32_t seq = 0;
    std::uint32_t session = 0;
    std::uint64_t length = 0;
};
#pragma pack(pop)

struct NetPacket {
    Cmd cmd = Cmd::Error;
    std::uint32_t seq = 0;
    std::uint32_t session = 0;
    std::string body;
};

struct PermissionRule {
    std::string prefix;
    std::uint32_t bits = 0;
};

struct UserRecord {
    std::string username;
    std::string passwordHash;
    std::string home = "/";
    std::string ruleSpec;
    std::vector<PermissionRule> rules;
    bool enabled = true;
    bool admin = false;
};

struct SessionInfo {
    std::uint32_t id = 0;
    std::string username;
    std::string home = "/";
    std::vector<PermissionRule> rules;
    bool anonymous = false;
    bool admin = false;
};

struct FileEntry {
    std::string name;
    std::string path;
    std::string mtime;
    std::uint64_t size = 0;
    bool isDir = false;
};

bool InitSockets();
void CleanupSockets();
SOCKET CreateListenSocket(int port);
SOCKET ConnectSocket(const std::string& host, int port);
void CloseSocket(SOCKET& sock);

std::wstring Utf8ToWide(const std::string& value);
std::string WideToUtf8(const std::wstring& value);
std::string Trim(const std::string& value);
std::vector<std::string> Split(const std::string& value, char ch);

std::string EscapeField(const std::string& value);
std::string UnescapeField(const std::string& value);
std::string MakeLine(const std::vector<std::string>& fields);
std::vector<std::vector<std::string>> ParseLines(const std::string& text);
std::string SerializePairs(const std::map<std::string, std::string>& pairs);
std::map<std::string, std::string> ParsePairs(const std::string& text);

bool SendAll(SOCKET sock, const void* data, std::size_t len);
bool RecvAll(SOCKET sock, void* data, std::size_t len);
bool SendPacket(SOCKET sock, Cmd cmd, std::uint32_t seq, std::uint32_t session, const std::string& body);
bool RecvPacket(SOCKET sock, NetPacket& packet);

std::string Sha256Bytes(const unsigned char* data, std::size_t len);
std::string Sha256String(const std::string& text);
std::string Sha256File(const std::filesystem::path& path);
std::string NowString();
std::string FormatBytes(std::uint64_t bytes);

std::string NormalizeVirtualPath(const std::string& raw);
std::filesystem::path VirtualToReal(const std::filesystem::path& root, const std::string& virtualPath);
bool ParentExistsForWrite(const std::filesystem::path& path);

std::uint32_t ParsePermBits(const std::string& text);
std::string FormatPermBits(std::uint32_t bits);
std::vector<PermissionRule> ParseRuleSpec(const std::string& spec);
bool HasPermission(const std::vector<PermissionRule>& rules, const std::string& path, std::uint32_t bit);

std::vector<UserRecord> LoadUsers(const std::filesystem::path& path);
bool SaveUsers(const std::filesystem::path& path, const std::vector<UserRecord>& users);

std::vector<FileEntry> EnumerateDirectory(const std::filesystem::path& base, const std::string& virtualPath);
std::string FileTimeString(const std::filesystem::file_time_type& fileTime);

}  // namespace fds
