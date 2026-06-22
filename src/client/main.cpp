#include "protocol.hpp"
#include "window.hpp"
#include "platform/win32_util.hpp"

#include <iostream>

namespace {

using fds::clientapp::CommandClient;
using fds::clientapp::ConnectionInfo;
using fds::clientapp::DownloadFileSync;
using fds::clientapp::FlowControl;
using fds::clientapp::TransferOutcome;
using fds::clientapp::TransferResult;
using fds::clientapp::UploadFileSync;

struct ScriptOptions {
    std::string host = "127.0.0.1";
    int port = fds::kDefaultPort;
    std::string user = "demo";
    std::string pass = "demo123";
    bool anonymous = false;
    std::string listPath = "/";
    std::string uploadLocal;
    std::string uploadRemote;
    std::string downloadRemote;
    std::string downloadLocal;
};

ScriptOptions ParseScriptOptions(int argc, wchar_t** argv) {
    ScriptOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv[i];
        auto next = [&](std::string& out) {
            if (i + 1 < argc) {
                out = fds::WideToUtf8(argv[++i]);
            }
        };

        if (arg == L"--host") next(options.host);
        else if (arg == L"--port" && i + 1 < argc) options.port = _wtoi(argv[++i]);
        else if (arg == L"--user") next(options.user);
        else if (arg == L"--pass") next(options.pass);
        else if (arg == L"--anonymous") options.anonymous = true;
        else if (arg == L"--list") next(options.listPath);
        else if (arg == L"--upload-local") next(options.uploadLocal);
        else if (arg == L"--upload-remote") next(options.uploadRemote);
        else if (arg == L"--download-remote") next(options.downloadRemote);
        else if (arg == L"--download-local") next(options.downloadLocal);
    }
    return options;
}

int RunScript(int argc, wchar_t** argv) {
    const ScriptOptions options = ParseScriptOptions(argc, argv);

    CommandClient client;
    std::string error;
    if (!client.Connect(options.host, options.port, options.user, options.pass, options.anonymous, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    const ConnectionInfo connection{options.host, options.port, client.Session()};
    if (!options.uploadLocal.empty() && !options.uploadRemote.empty()) {
        const TransferResult result =
            UploadFileSync(connection, fds::Utf8ToWide(options.uploadLocal), options.uploadRemote,
                           [](std::uint64_t, std::uint64_t) { return FlowControl::Continue; });
        if (result.outcome != TransferOutcome::Success) {
            std::cerr << result.message << "\n";
            return 1;
        }
        std::cout << "upload ok\n";
    }

    if (!options.downloadRemote.empty() && !options.downloadLocal.empty()) {
        const TransferResult result =
            DownloadFileSync(connection, options.downloadRemote, fds::Utf8ToWide(options.downloadLocal),
                             [](std::uint64_t, std::uint64_t) { return FlowControl::Continue; });
        if (result.outcome != TransferOutcome::Success) {
            std::cerr << result.message << "\n";
            return 1;
        }
        std::cout << "download ok\n";
    }

    std::string cwd;
    std::vector<fds::FileEntry> items;
    if (!client.List(options.listPath, cwd, items, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    std::cout << "cwd: " << cwd << "\n";
    for (const auto& item : items) {
        std::cout << (item.isDir ? "[D] " : "[F] ") << item.path << "  " << item.mtime << "  " << item.size << "\n";
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    fds::win32::ScopedSockets sockets;
    fds::win32::ScopedOle ole;

    for (int i = 1; i < argc; ++i) {
        if (std::wstring_view(argv[i]) == L"--script") {
            return RunScript(argc, argv);
        }
    }

    fds::clientapp::ClientWindow app(GetModuleHandleW(nullptr));
    return app.Run(SW_SHOW);
}
