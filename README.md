# myFtpPlus

`myFtpPlus` 是一个基于 `C++20 + Win32 GUI + Winsock TCP` 的 Windows 远程文件传输示例项目，包含图形化服务端、图形化客户端，以及便于联调的无界面运行模式。

当前实现覆盖的核心能力：

- 服务端 `select()` 主循环，上传/下载使用独立工作线程
- 图形化服务端管理面板，可启动/停止服务、管理用户、浏览服务端目录、查看传输列表
- 图形化客户端，可登录、浏览远程目录、上传、下载、创建目录、删除、重命名
- 普通用户登录和匿名登录
- 基于规则字符串的目录权限控制
- 上传/下载断点续传
- 传输结束后做 `SHA-256` 校验
- 客户端任务列表，支持暂停、继续、取消
- 客户端常用连接保存
- 服务端用户持久化到 SQLite，兼容旧版 TSV 导入

## 目录结构

- `src/shared/`
  公共协议、路径处理、权限规则、SHA-256、文件与网络工具。
- `src/platform/`
  Win32 辅助封装。
- `src/server/`
  服务端核心、用户存储、管理员窗口。
- `src/client/`
  客户端协议、任务状态、图形界面。
- `build.ps1`
  PowerShell 构建脚本。
- `bin/`
  构建输出目录，生成 `server.exe` 和 `client.exe`。
- `data/`
  运行期数据目录。服务端根目录、用户数据库、客户端收藏连接都会写到这里。

## 构建环境

- Windows
- `g++`，支持 `-std=gnu++20`
- PowerShell
- SQLite 运行时

服务端启动时会优先加载系统自带的 `winsqlite3.dll`，如果系统没有，也可以放置 `sqlite3.dll` 供程序动态加载。

## 构建

在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

生成文件：

- `bin/server.exe`
- `bin/client.exe`

## 运行方式

### 图形界面

服务端：

```powershell
.\bin\server.exe
```

客户端：

```powershell
.\bin\client.exe
```

默认端口是 `9527`。

### 服务端无界面模式

适合本地联调或脚本启动：

```powershell
.\bin\server.exe --headless --port 9527
```

### 客户端脚本模式

客户端支持 `--script` 模式，可直接登录、上传、下载并列目录。

常用参数：

- `--host`
- `--port`
- `--user`
- `--pass`
- `--anonymous`
- `--list`
- `--upload-local`
- `--upload-remote`
- `--download-remote`
- `--download-local`

列目录：

```powershell
.\bin\client.exe --script --host 127.0.0.1 --port 9527 --user demo --pass demo123 --list /
```

上传并列目录：

```powershell
.\bin\client.exe --script --host 127.0.0.1 --port 9527 --user demo --pass demo123 `
  --upload-local smoke_upload.txt `
  --upload-remote /upload/demo/smoke_upload.txt `
  --list /upload/demo
```

下载并列目录：

```powershell
.\bin\client.exe --script --host 127.0.0.1 --port 9527 --user demo --pass demo123 `
  --download-remote /upload/demo/smoke_upload.txt `
  --download-local smoke_download.txt `
  --list /upload/demo
```

匿名登录列目录：

```powershell
.\bin\client.exe --script --host 127.0.0.1 --port 9527 --anonymous --list /public
```

## 默认数据与目录

服务端首次启动会在 `data/server_root/` 下准备这些目录：

- `public/`
- `download/`
- `upload/`
- `users/`

并创建欢迎文件：

- `data/server_root/public/welcome.txt`

默认账号：

- 管理员：`admin / admin123`
- 普通用户：`demo / demo123`
- 匿名用户：无需账号密码，只能访问公共只读区域

客户端会把常用连接保存到：

- `data/client_favorites.tsv`

服务端用户数据默认保存到：

- `data/users.db`

如果目录里存在旧版 `data/users.tsv`，服务端会在首次加载时尝试导入。

## 权限规则格式

权限规则使用 `/path:RWDN` 形式，多个规则用分号分隔，例如：

```text
/public:R;/download:R;/users/demo:RWDN;/upload/demo:RWDN
```

含义：

- `R`：读取，包含浏览目录和下载
- `W`：写入，包含上传和创建目录
- `D`：删除
- `N`：重命名

普通用户默认规则与代码中的种子用户一致：

```text
/public:R;/download:R;/users/<username>:RWDN;/upload/<username>:RWDN
```

管理员规则为：

```text
/:RWDN
```

## 说明

- 服务端真实文件根目录固定映射到 `data/server_root/`。
- 客户端暂停任务时会主动中断当前传输；继续时会重新建连，并从已完成偏移继续。
- 失败任务如果已经传输了一部分数据，客户端会保留本地进度，支持后续续传。
- 图形界面中的用户、目录和传输列表会定时刷新。
