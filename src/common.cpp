#include "common.hpp"

#include <ws2tcpip.h>

#include <array>
#include <charconv>
#include <cstdio>
#include <system_error>

namespace fds {

namespace {

std::uint64_t HostToNet64(std::uint64_t value) {
    const auto hi = htonl(static_cast<std::uint32_t>(value >> 32));
    const auto lo = htonl(static_cast<std::uint32_t>(value & 0xffffffffull));
    return (static_cast<std::uint64_t>(lo) << 32) | hi;
}

std::uint64_t NetToHost64(std::uint64_t value) {
    const auto hi = ntohl(static_cast<std::uint32_t>(value >> 32));
    const auto lo = ntohl(static_cast<std::uint32_t>(value & 0xffffffffull));
    return (static_cast<std::uint64_t>(lo) << 32) | hi;
}

std::string ToHex(const unsigned char* data, std::size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

bool EnsureDir(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return true;
    }
    return std::filesystem::create_directories(path, ec);
}

}  // namespace

bool InitSockets() {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void CleanupSockets() {
    WSACleanup();
}

void CloseSocket(SOCKET& sock) {
    if (sock != INVALID_SOCKET) {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
}

SOCKET CreateListenSocket(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    u_long yes = 1;
    ioctlsocket(sock, FIONBIO, &yes);
    BOOL reuse = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    if (listen(sock, SOMAXCONN) != 0) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

SOCKET ConnectSocket(const std::string& host, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
            closesocket(sock);
            return INVALID_SOCKET;
        }
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
        freeaddrinfo(result);
    }
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::string Trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> Split(const std::string& value, char ch) {
    std::vector<std::string> items;
    std::string current;
    for (char c : value) {
        if (c == ch) {
            items.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    items.push_back(current);
    return items;
}

std::string EscapeField(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string UnescapeField(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool esc = false;
    for (char ch : value) {
        if (!esc) {
            if (ch == '\\') {
                esc = true;
            } else {
                out.push_back(ch);
            }
            continue;
        }
        esc = false;
        switch (ch) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            default: out.push_back(ch); break;
        }
    }
    if (esc) {
        out.push_back('\\');
    }
    return out;
}

std::string MakeLine(const std::vector<std::string>& fields) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) {
            oss << '\t';
        }
        oss << EscapeField(fields[i]);
    }
    oss << '\n';
    return oss.str();
}

std::vector<std::vector<std::string>> ParseLines(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) {
            continue;
        }
        auto cols = Split(line, '\t');
        for (auto& col : cols) {
            col = UnescapeField(col);
        }
        rows.push_back(std::move(cols));
    }
    return rows;
}

std::string SerializePairs(const std::map<std::string, std::string>& pairs) {
    std::ostringstream oss;
    for (const auto& [key, value] : pairs) {
        oss << EscapeField(key) << '\t' << EscapeField(value) << '\n';
    }
    return oss.str();
}

std::map<std::string, std::string> ParsePairs(const std::string& text) {
    std::map<std::string, std::string> out;
    for (const auto& cols : ParseLines(text)) {
        if (cols.size() >= 2) {
            out[cols[0]] = cols[1];
        }
    }
    return out;
}

bool SendAll(SOCKET sock, const void* data, std::size_t len) {
    const auto* ptr = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < len) {
        const int n = send(sock, ptr + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool RecvAll(SOCKET sock, void* data, std::size_t len) {
    auto* ptr = static_cast<char*>(data);
    std::size_t recvd = 0;
    while (recvd < len) {
        const int n = recv(sock, ptr + recvd, static_cast<int>(len - recvd), 0);
        if (n <= 0) {
            return false;
        }
        recvd += static_cast<std::size_t>(n);
    }
    return true;
}

bool SendPacket(SOCKET sock, Cmd cmd, std::uint32_t seq, std::uint32_t session, const std::string& body) {
    PacketHeader header{};
    header.magic = htonl(kMagic);
    header.version = htons(kVersion);
    header.cmd = htons(static_cast<std::uint16_t>(cmd));
    header.seq = htonl(seq);
    header.session = htonl(session);
    header.length = HostToNet64(static_cast<std::uint64_t>(body.size()));
    if (!SendAll(sock, &header, sizeof(header))) {
        return false;
    }
    return body.empty() || SendAll(sock, body.data(), body.size());
}

bool RecvPacket(SOCKET sock, NetPacket& packet) {
    PacketHeader header{};
    if (!RecvAll(sock, &header, sizeof(header))) {
        return false;
    }
    if (ntohl(header.magic) != kMagic || ntohs(header.version) != kVersion) {
        return false;
    }
    const auto length = static_cast<std::size_t>(NetToHost64(header.length));
    packet.cmd = static_cast<Cmd>(ntohs(header.cmd));
    packet.seq = ntohl(header.seq);
    packet.session = ntohl(header.session);
    packet.body.assign(length, '\0');
    return length == 0 || RecvAll(sock, packet.body.data(), length);
}

std::string Sha256Bytes(const unsigned char* data, std::size_t len) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objLen = 0;
    DWORD hashLen = 0;
    DWORD bytes = 0;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return "";
    }
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &bytes, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen), sizeof(hashLen), &bytes, 0);
    std::vector<unsigned char> obj(objLen);
    std::vector<unsigned char> digest(hashLen);
    if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return "";
    }
    BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(len), 0);
    BCryptFinishHash(hash, digest.data(), hashLen, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ToHex(digest.data(), digest.size());
}

std::string Sha256String(const std::string& text) {
    return Sha256Bytes(reinterpret_cast<const unsigned char*>(text.data()), text.size());
}

std::string Sha256File(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "";
    }
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objLen = 0;
    DWORD hashLen = 0;
    DWORD bytes = 0;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return "";
    }
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &bytes, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen), sizeof(hashLen), &bytes, 0);
    std::vector<unsigned char> obj(objLen);
    std::vector<unsigned char> digest(hashLen);
    if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return "";
    }

    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = in.gcount();
        if (got > 0) {
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(got), 0);
        }
    }

    BCryptFinishHash(hash, digest.data(), hashLen, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ToHex(digest.data(), digest.size());
}

std::string NowString() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

std::string FormatBytes(std::uint64_t bytes) {
    constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit < std::size(units) - 1) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << ' ' << units[unit];
    return oss.str();
}

std::string NormalizeVirtualPath(const std::string& raw) {
    std::string value = Trim(raw);
    if (value.empty()) {
        return "/";
    }
    std::replace(value.begin(), value.end(), '\\', '/');
    if (value.front() != '/') {
        value.insert(value.begin(), '/');
    }
    std::vector<std::string> parts;
    for (const auto& part : Split(value, '/')) {
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            return "";
        }
        parts.push_back(part);
    }
    if (parts.empty()) {
        return "/";
    }
    std::ostringstream oss;
    for (const auto& part : parts) {
        oss << '/' << part;
    }
    return oss.str();
}

std::filesystem::path VirtualToReal(const std::filesystem::path& root, const std::string& virtualPath) {
    const auto clean = NormalizeVirtualPath(virtualPath);
    if (clean.empty()) {
        return {};
    }
    std::filesystem::path out = root;
    for (const auto& part : Split(clean, '/')) {
        if (!part.empty()) {
            out /= Utf8ToWide(part);
        }
    }
    return out.lexically_normal();
}

bool ParentExistsForWrite(const std::filesystem::path& path) {
    std::error_code ec;
    auto parent = path.parent_path();
    if (parent.empty()) {
        return false;
    }
    return EnsureDir(parent) && std::filesystem::exists(parent, ec);
}

std::uint32_t ParsePermBits(const std::string& text) {
    std::uint32_t bits = 0;
    for (char ch : text) {
        switch (std::toupper(static_cast<unsigned char>(ch))) {
            case 'R': bits |= PermRead; break;
            case 'W': bits |= PermWrite; break;
            case 'D': bits |= PermDelete; break;
            case 'N': bits |= PermRename; break;
            default: break;
        }
    }
    return bits;
}

std::string FormatPermBits(std::uint32_t bits) {
    std::string out;
    if (bits & PermRead) out.push_back('R');
    if (bits & PermWrite) out.push_back('W');
    if (bits & PermDelete) out.push_back('D');
    if (bits & PermRename) out.push_back('N');
    return out;
}

std::vector<PermissionRule> ParseRuleSpec(const std::string& spec) {
    std::vector<PermissionRule> rules;
    for (const auto& item : Split(spec, ';')) {
        const auto rule = Trim(item);
        if (rule.empty()) {
            continue;
        }
        const auto pos = rule.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        auto prefix = NormalizeVirtualPath(rule.substr(0, pos));
        if (prefix.empty()) {
            continue;
        }
        rules.push_back({prefix, ParsePermBits(rule.substr(pos + 1))});
    }
    return rules;
}

bool HasPermission(const std::vector<PermissionRule>& rules, const std::string& path, std::uint32_t bit) {
    const auto clean = NormalizeVirtualPath(path);
    if (clean.empty()) {
        return false;
    }
    const PermissionRule* best = nullptr;
    for (const auto& rule : rules) {
        if (clean == rule.prefix ||
            (rule.prefix == "/") ||
            (clean.size() > rule.prefix.size() && clean.rfind(rule.prefix + "/", 0) == 0)) {
            if (!best || rule.prefix.size() > best->prefix.size()) {
                best = &rule;
            }
        }
    }
    return best && ((best->bits & bit) == bit);
}

std::vector<UserRecord> LoadUsers(const std::filesystem::path& path) {
    std::vector<UserRecord> users;
    std::ifstream in(path);
    if (!in) {
        return users;
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    for (const auto& cols : ParseLines(oss.str())) {
        if (cols.size() < 6) {
            continue;
        }
        UserRecord user;
        user.username = cols[0];
        user.passwordHash = cols[1];
        user.enabled = cols[2] == "1";
        user.home = NormalizeVirtualPath(cols[3]);
        user.admin = cols[4] == "1";
        user.ruleSpec = cols[5];
        user.rules = ParseRuleSpec(user.ruleSpec);
        users.push_back(std::move(user));
    }
    return users;
}

bool SaveUsers(const std::filesystem::path& path, const std::vector<UserRecord>& users) {
    if (!ParentExistsForWrite(path)) {
        return false;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto& user : users) {
        out << MakeLine({
            user.username,
            user.passwordHash,
            user.enabled ? "1" : "0",
            NormalizeVirtualPath(user.home),
            user.admin ? "1" : "0",
            user.ruleSpec,
        });
    }
    return true;
}

std::string FileTimeString(const std::filesystem::file_time_type& fileTime) {
    const auto sysNow = std::chrono::system_clock::now();
    const auto fileNow = std::filesystem::file_time_type::clock::now();
    const auto sysTime = sysNow + (fileTime - fileNow);
    const std::time_t tt = std::chrono::system_clock::to_time_t(sysTime);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::vector<FileEntry> EnumerateDirectory(const std::filesystem::path& base, const std::string& virtualPath) {
    std::vector<FileEntry> entries;
    std::error_code ec;
    for (const auto& it : std::filesystem::directory_iterator(base, ec)) {
        if (ec) {
            break;
        }
        FileEntry item;
        item.name = WideToUtf8(it.path().filename().wstring());
        item.path = NormalizeVirtualPath(virtualPath + "/" + item.name);
        item.isDir = it.is_directory(ec);
        item.size = item.isDir ? 0 : it.file_size(ec);
        item.mtime = FileTimeString(it.last_write_time(ec));
        entries.push_back(std::move(item));
    }
    std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDir != b.isDir) {
            return a.isDir > b.isDir;
        }
        return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return entries;
}

}  // namespace fds
