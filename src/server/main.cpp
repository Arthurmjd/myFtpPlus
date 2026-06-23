#include "core.hpp"
#include "window.hpp"
#include "platform/win32_util.hpp"

#include <iostream>

namespace {

// 解析无界面模式下传入的端口参数。
int ParsePortArg(int argc, wchar_t** argv) {
    int port = fds::kDefaultPort;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring_view(argv[i]) == L"--port" && i + 1 < argc) {
            port = _wtoi(argv[++i]);
        }
    }
    return port > 0 ? port : fds::kDefaultPort;
}

// 服务端脚本/守护运行入口，不创建图形界面。
int RunHeadless(int argc, wchar_t** argv) {
    const int port = ParsePortArg(argc, argv);

    fds::serverapp::ServerCore core;
    std::string error;
    if (!core.Start(port, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    std::wcout << L"FDS server is running on port " << port << L". Press Ctrl+C to stop.\n";
    while (true) {
        Sleep(1000);
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    // 整个进程只需要一份 Winsock 生命周期管理。
    fds::win32::ScopedSockets sockets;

    for (int i = 1; i < argc; ++i) {
        if (std::wstring_view(argv[i]) == L"--headless") {
            return RunHeadless(argc, argv);
        }
    }

    // 默认启动图形化管理员面板。
    fds::serverapp::ServerWindow app(GetModuleHandleW(nullptr));
    return app.Run(SW_SHOW);
}
