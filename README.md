# myFtpPlus

基于需求设计文档实现的简化版远程文件下载系统，采用 `C++ + Win32 GUI + Winsock TCP`，包含：

- 服务端 `select()` 主循环
- 客户端 / 服务端图形界面
- 用户登录与匿名访问
- 远程目录浏览、创建目录、删除、重命名
- 上传 / 下载、SHA-256 校验
- 断点续传
- 多任务列表、暂停、继续、取消
- 服务端管理员面板、用户权限、操作日志

## 目录说明

- `src/common.*`：协议、哈希、权限、文件工具
- `src/server.cpp`：服务端和管理员面板
- `src/client.cpp`：客户端、任务管理和脚本模式
- `build.ps1`：VSCode / PowerShell 构建脚本

## 构建

在 VSCode 中直接执行默认任务 `Build All`，或在项目根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

生成文件位于：

- `bin/server.exe`
- `bin/client.exe`

## 运行

推荐直接使用 VSCode 任务：

- `Run Server`
- `Run Client`

也可以在项目根目录手动运行：

```powershell
.\bin\server.exe
.\bin\client.exe
```

默认测试账号：

- 管理员：`admin / admin123`
- 普通用户：`demo / demo123`

## 脚本模式

客户端支持无界面联调：

```powershell
.\bin\client.exe --script --host 127.0.0.1 --port 9527 --user demo --pass demo123 --list /public
```

上传：

```powershell
.\bin\client.exe --script --host 127.0.0.1 --port 9527 --user demo --pass demo123 `
  --upload-local smoke_upload.txt --upload-remote /upload/demo/smoke_upload.txt --list /upload/demo
```

下载：

```powershell
.\bin\client.exe --script --host 127.0.0.1 --port 9527 --user demo --pass demo123 `
  --download-remote /upload/demo/smoke_upload.txt --download-local smoke_download.txt --list /upload/demo
```

## 权限规则格式

权限规则使用：

```text
/path:RWDN;/other:R
```

含义：

- `R`：浏览 / 下载
- `W`：上传 / 创建目录
- `D`：删除
- `N`：重命名

示例：

```text
/public:R;/download:R;/users/demo:RWDN;/upload/demo:RWDN
```

## 说明

- 断点续传通过“记录已传输偏移量 + 重新连接继续传输”实现。
- 暂停任务本质上是主动断开当前传输连接；继续时会自动按偏移量重连。
- 取消任务会停止当前传输，已传的部分文件会保留，便于后续续传或手动删除。

