#include "protocol.hpp"

#include <cstdlib>
#include <fstream>

namespace fds::clientapp {

namespace {

class SocketGuard {
public:
    explicit SocketGuard(SOCKET sock = INVALID_SOCKET) : sock_(sock) {}

    ~SocketGuard() {
        CloseSocket(sock_);
    }

    SOCKET Get() const {
        return sock_;
    }

private:
    SOCKET sock_;
};

std::string PacketMessage(const NetPacket& packet, const std::string& fallback) {
    const auto pairs = ParsePairs(packet.body);
    const auto it = pairs.find("message");
    return it == pairs.end() ? fallback : it->second;
}

bool ReceiveExpected(SOCKET sock, Cmd expected, NetPacket& packet, const std::string& disconnectedMessage,
                     std::string& error) {
    if (!RecvPacket(sock, packet)) {
        error = disconnectedMessage;
        return false;
    }
    if (packet.cmd == Cmd::Error) {
        error = PacketMessage(packet, "服务器返回了错误");
        return false;
    }
    if (packet.cmd != expected) {
        error = "服务器返回了未知响应";
        return false;
    }
    return true;
}

TransferResult MakeResult(TransferOutcome outcome, std::string message = {}) {
    return {outcome, std::move(message)};
}

}  // namespace

CommandClient::~CommandClient() {
    Disconnect();
}

bool CommandClient::Connect(const std::string& host, int port, const std::string& user, const std::string& password,
                            bool anonymous, std::string& error) {
    Disconnect();

    sock_ = ConnectSocket(host, port);
    if (sock_ == INVALID_SOCKET) {
        error = "连接服务器失败";
        return false;
    }

    host_ = host;
    port_ = port;

    std::string body;
    if (!Call(Cmd::Login,
              {
                  {"anonymous", anonymous ? "1" : "0"},
                  {"username", user},
                  {"password", password},
              },
              Cmd::LoginOk, body, error)) {
        Disconnect();
        return false;
    }

    const auto pairs = ParsePairs(body);
    const auto sessionIt = pairs.find("session");
    const auto homeIt = pairs.find("home");
    const auto userIt = pairs.find("username");
    const auto adminIt = pairs.find("admin");
    if (sessionIt == pairs.end() || homeIt == pairs.end() || userIt == pairs.end() || adminIt == pairs.end()) {
        error = "服务器登录响应不完整";
        Disconnect();
        return false;
    }

    session_ = static_cast<std::uint32_t>(std::strtoul(sessionIt->second.c_str(), nullptr, 10));
    home_ = homeIt->second;
    user_ = userIt->second;
    admin_ = adminIt->second == "1";
    return true;
}

void CommandClient::Disconnect() {
    if (sock_ != INVALID_SOCKET && session_ != 0) {
        SendPacket(sock_, Cmd::Logout, nextSeq_++, session_, "");
    }
    CloseSocket(sock_);
    session_ = 0;
    nextSeq_ = 1;
    port_ = kDefaultPort;
    admin_ = false;
    host_.clear();
    home_.clear();
    user_.clear();
}

bool CommandClient::Connected() const {
    return sock_ != INVALID_SOCKET && session_ != 0;
}

std::uint32_t CommandClient::Session() const {
    return session_;
}

const std::string& CommandClient::Host() const {
    return host_;
}

int CommandClient::Port() const {
    return port_;
}

const std::string& CommandClient::Home() const {
    return home_;
}

const std::string& CommandClient::Username() const {
    return user_;
}

bool CommandClient::IsAdmin() const {
    return admin_;
}

bool CommandClient::List(const std::string& path, std::string& cwd, std::vector<FileEntry>& items, std::string& error) {
    std::string body;
    if (!Call(Cmd::List, {{"path", path}}, Cmd::ListResult, body, error)) {
        return false;
    }

    cwd.clear();
    items.clear();
    for (const auto& row : ParseLines(body)) {
        if (row.empty()) {
            continue;
        }
        if (row[0] == "PWD" && row.size() >= 2) {
            cwd = row[1];
            continue;
        }
        if (row[0] != "E" || row.size() < 6) {
            continue;
        }

        FileEntry entry;
        entry.name = row[1];
        entry.path = row[2];
        entry.isDir = row[3] == "1";
        entry.size = std::strtoull(row[4].c_str(), nullptr, 10);
        entry.mtime = row[5];
        items.push_back(std::move(entry));
    }
    return true;
}

bool CommandClient::MakeDir(const std::string& path, std::string& error) {
    std::string body;
    return Call(Cmd::MakeDir, {{"path", path}}, Cmd::Ok, body, error);
}

bool CommandClient::Remove(const std::string& path, std::string& error) {
    std::string body;
    return Call(Cmd::Remove, {{"path", path}}, Cmd::Ok, body, error);
}

bool CommandClient::Rename(const std::string& path, const std::string& newName, std::string& error) {
    std::string body;
    return Call(Cmd::Rename, {{"path", path}, {"new_name", newName}}, Cmd::Ok, body, error);
}

bool CommandClient::Call(Cmd cmd, const std::map<std::string, std::string>& request, Cmd expected, std::string& body,
                         std::string& error) {
    if (sock_ == INVALID_SOCKET) {
        error = "尚未连接";
        return false;
    }

    if (!SendPacket(sock_, cmd, nextSeq_++, session_, SerializePairs(request))) {
        error = "请求发送失败";
        return false;
    }

    NetPacket packet;
    if (!ReceiveExpected(sock_, expected, packet, "服务器无响应", error)) {
        return false;
    }

    body = packet.body;
    return true;
}

TransferResult UploadFileSync(const ConnectionInfo& connection, const std::filesystem::path& localPath,
                              const std::string& remotePath, ProgressCallback onProgress) {
    std::error_code ec;
    const auto total = std::filesystem::file_size(localPath, ec);
    if (ec) {
        return MakeResult(TransferOutcome::Failed, "本地文件不可读");
    }

    const auto localHash = Sha256File(localPath);
    if (localHash.empty()) {
        return MakeResult(TransferOutcome::Failed, "无法计算本地文件哈希");
    }

    std::ifstream in(localPath, std::ios::binary);
    if (!in) {
        return MakeResult(TransferOutcome::Failed, "本地文件无法打开");
    }

    SocketGuard sock(ConnectSocket(connection.host, connection.port));
    if (sock.Get() == INVALID_SOCKET) {
        return MakeResult(TransferOutcome::Failed, "上传连接失败");
    }

    if (!SendPacket(sock.Get(), Cmd::UploadBegin, 1, connection.session,
                    SerializePairs({
                        {"path", remotePath},
                        {"size", std::to_string(total)},
                        {"sha256", localHash},
                    }))) {
        return MakeResult(TransferOutcome::Failed, "发送上传请求失败");
    }

    NetPacket packet;
    std::string error;
    if (!ReceiveExpected(sock.Get(), Cmd::UploadReady, packet, "服务器未响应上传请求", error)) {
        return MakeResult(TransferOutcome::Failed, std::move(error));
    }

    const auto meta = ParsePairs(packet.body);
    const auto offsetIt = meta.find("offset");
    if (offsetIt == meta.end()) {
        return MakeResult(TransferOutcome::Failed, "服务器未返回续传偏移");
    }

    const std::uint64_t offset = std::strtoull(offsetIt->second.c_str(), nullptr, 10);
    if (offset > total) {
        return MakeResult(TransferOutcome::Failed, "服务器返回的续传偏移异常");
    }

    in.seekg(static_cast<std::streamoff>(offset));
    std::vector<char> buffer(kChunkSize);
    std::uint64_t done = offset;
    while (in) {
        if (const auto control = onProgress(done, total); control != FlowControl::Continue) {
            return control == FlowControl::Pause ? MakeResult(TransferOutcome::Paused)
                                                 : MakeResult(TransferOutcome::Cancelled);
        }

        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = in.gcount();
        if (got <= 0) {
            break;
        }

        if (!SendPacket(sock.Get(), Cmd::UploadData, 1, connection.session,
                        std::string(buffer.data(), static_cast<std::size_t>(got)))) {
            return MakeResult(TransferOutcome::Failed, "上传数据发送失败");
        }

        done += static_cast<std::uint64_t>(got);
    }

    if (!SendPacket(sock.Get(), Cmd::UploadEnd, 1, connection.session, SerializePairs({{"sha256", localHash}}))) {
        return MakeResult(TransferOutcome::Failed, "上传结束通知失败");
    }

    if (!ReceiveExpected(sock.Get(), Cmd::UploadDone, packet, "服务器未返回校验结果", error)) {
        return MakeResult(TransferOutcome::Failed, std::move(error));
    }

    const auto result = ParsePairs(packet.body);
    const auto hashIt = result.find("sha256");
    if (hashIt == result.end() || hashIt->second != localHash) {
        return MakeResult(TransferOutcome::Failed, "上传后的哈希校验失败");
    }

    return MakeResult(TransferOutcome::Success);
}

TransferResult DownloadFileSync(const ConnectionInfo& connection, const std::string& remotePath,
                                const std::filesystem::path& localPath, ProgressCallback onProgress) {
    if (!localPath.parent_path().empty()) {
        std::filesystem::create_directories(localPath.parent_path());
    }

    std::error_code ec;
    std::uint64_t offset = std::filesystem::exists(localPath, ec) ? std::filesystem::file_size(localPath, ec) : 0;

    SocketGuard sock(ConnectSocket(connection.host, connection.port));
    if (sock.Get() == INVALID_SOCKET) {
        return MakeResult(TransferOutcome::Failed, "下载连接失败");
    }

    if (!SendPacket(sock.Get(), Cmd::DownloadBegin, 1, connection.session,
                    SerializePairs({
                        {"path", remotePath},
                        {"offset", std::to_string(offset)},
                    }))) {
        return MakeResult(TransferOutcome::Failed, "发送下载请求失败");
    }

    NetPacket packet;
    std::string error;
    if (!ReceiveExpected(sock.Get(), Cmd::DownloadMeta, packet, "服务器未响应下载请求", error)) {
        return MakeResult(TransferOutcome::Failed, std::move(error));
    }

    const auto meta = ParsePairs(packet.body);
    const auto sizeIt = meta.find("size");
    const auto offsetIt = meta.find("offset");
    if (sizeIt == meta.end() || offsetIt == meta.end()) {
        return MakeResult(TransferOutcome::Failed, "服务器下载响应不完整");
    }

    const std::uint64_t total = std::strtoull(sizeIt->second.c_str(), nullptr, 10);
    offset = std::strtoull(offsetIt->second.c_str(), nullptr, 10);

    std::ofstream out;
    out.open(localPath, offset == 0 ? (std::ios::binary | std::ios::trunc) : (std::ios::binary | std::ios::app));
    if (!out) {
        return MakeResult(TransferOutcome::Failed, "本地文件无法写入");
    }

    std::uint64_t done = offset;
    while (true) {
        if (!RecvPacket(sock.Get(), packet)) {
            return MakeResult(TransferOutcome::Failed, "下载连接中断");
        }

        if (packet.cmd == Cmd::DownloadData) {
            out.write(packet.body.data(), static_cast<std::streamsize>(packet.body.size()));
            done += static_cast<std::uint64_t>(packet.body.size());
            if (const auto control = onProgress(done, total); control != FlowControl::Continue) {
                return control == FlowControl::Pause ? MakeResult(TransferOutcome::Paused)
                                                     : MakeResult(TransferOutcome::Cancelled);
            }
            continue;
        }

        if (packet.cmd == Cmd::DownloadDone) {
            out.flush();
            const auto result = ParsePairs(packet.body);
            const auto hashIt = result.find("sha256");
            if (hashIt == result.end() || Sha256File(localPath) != hashIt->second) {
                return MakeResult(TransferOutcome::Failed, "下载后的哈希校验失败");
            }
            return MakeResult(TransferOutcome::Success);
        }

        if (packet.cmd == Cmd::Error) {
            return MakeResult(TransferOutcome::Failed, PacketMessage(packet, "下载失败"));
        }

        return MakeResult(TransferOutcome::Failed, "服务器返回了未知响应");
    }
}

}  // namespace fds::clientapp
