# RemoteLink

简体中文 | [English](README.md)

RemoteLink 是一款可自行部署、通过浏览器使用的 RDP、VNC 和 SSH 远程连接工具。RDP 使用 FreeRDP 与 WebRTC，VNC 使用 noVNC Core 和服务端 WSS-to-RFB 代理，SSH 使用 xterm.js 和服务端 libssh2 代理。浏览器不会接收设备密码、私钥或证书。

网关以单个 Linux 服务运行。用户只需要现代浏览器，不需要浏览器扩展或桌面客户端。

## 功能

- 基于 WebRTC 的低延迟 H.264 画面和 Opus 声音
- 键盘、鼠标、滚轮、触摸、Unicode 文本粘贴、全屏和安全界面操作
- 多个预配置 RDP 目标，并通过主机白名单限制访问范围
- 用户名和密码登录，区分管理员与普通用户
- 在管理控制台创建、启停用户和重置密码
- 管理员统一添加连接，并按用户授权可访问的连接
- 普通用户只显示获授权连接，没有可用连接的协议标签会自动隐藏
- 按用户隔离的文件管理：文件夹、分块上传、下载、重命名、删除、配额和审计日志
- 通过 **Gateway Files** 映射 RDP 网关磁盘
- 打印机重定向，并将打印任务转换为可下载的 PDF
- 实时帧率、码率、分辨率、编解码器、声音、丢帧和流量状态
- 管理员控制台：服务状态、会话、事件和强制断开
- 普通用户不能访问管理员状态和断开操作
- 支持简体中文、繁体中文、英语、日语和韩语
- 默认跟随系统语言，也可手动选择并持久保存
- 可配置分辨率、码率、声音、打印和实验性硬件加速
- HTTPS/WSS、systemd 加密凭据和用户数据隔离
- 适配桌面与移动浏览器
- 独立 VNC 支持：使用 noVNC Core，通过一次性票据建立 WSS 到 RFB 的代理连接
- RemoteLink 自有 VNC 设备页、会话工具栏、连接管理和用户授权界面
- VNC 支持密码、用户名密码和 CA 证书三种认证模式，并按 RDP 策略自动重连
- 独立 SSH 终端，支持用户名密码和 PEM/OpenSSH 私钥认证，以及浏览家目录、上传、下载和删除的 SFTP 文件管理
- 统一查看 RDP、VNC、SSH 活动会话、最近活动和断开原因
- 三种协议均支持替换、清除凭据，并只显示“已配置”而不回显内容
- SSH 主机指纹必须经过管理员测试、核对并明确确认后才允许连接

## 架构

VNC 使用独立链路，不依赖 Guacamole 或 guacd：

```text
RemoteLink VNC 页面（noVNC Core，不含 noVNC UI）
  └─ WSS `/vnc/ws?ticket=一次性票据`
                  │
             RemoteLink
       WebSocket ↔ TCP/RFB 代理
                  │
            VNC 主机 :5900
```

管理员可在 `/vnc/admin` 维护 VNC 连接并为用户授权。VNC 地址和密码保存在服务端；
浏览器只在创建会话后获得 30 秒有效且只能消费一次的连接票据。VNC 密码不会写入 URL
或持久化到浏览器存储。

SSH 同样使用 30 秒有效的一次性 WSS 票据。密码、私钥和私钥口令仅保存在服务端的
属主可读凭据文件中，浏览器不会收到这些凭据。

从 0.2 升级到 0.3 不需要手工迁移数据，具体兼容行为和 SSH 指纹确认流程见
[`docs/upgrade-0.3.md`](docs/upgrade-0.3.md)。目标设备重装后，管理员需要在 SSH 连接管理中
执行“重置信任”，重新测试并确认新指纹。发布前可运行 `npm run test:release` 完成一键回归。
运行 `npm run test:e2e` 会启动隔离的 RDP、VNC、SSH 测试容器并执行回归；设置
`REMOTELINK_KEEP_TEST_STACK=1` 可在测试后保留容器用于人工检查。
脚本仅在未提供令牌时尝试测试环境的初始 `admin` / `admin` 账号；修改初始密码后，
请设置 `REMOTELINK_TEST_TOKEN`，或设置 `REMOTELINK_TEST_ADMIN_USERNAME` 和
`REMOTELINK_TEST_ADMIN_PASSWORD`。

```text
浏览器（纯 HTML/CSS/JavaScript）
  ├─ RDP：WSS 信令 + WebRTC H.264/Opus/数据 ── RemoteLink ── FreeRDP ── Windows
  ├─ VNC：一次性票据 WSS ───────────────────── RemoteLink ── TCP/RFB ─── VNC 服务
  └─ SSH：一次性票据 WSS + xterm.js ────────── RemoteLink ── libssh2 ─── SSH 服务
```

每个浏览器连接拥有独立的 RDP、画面捕获、编码器、声音和输入生命周期。不同用户之间不会广播画面或输入。

## 环境要求

- Linux（当前维护的部署平台）
- CMake 3.20 或更高版本，以及支持 C++20 的编译器
- FreeRDP 3 和 WinPR 3 开发包
- OpenH264、Opus、libyuv、OpenSSL、libssh2 和 libdatachannel
- 客户端浏览器信任的 TLS 证书
- TCP 18080 以及 WebRTC UDP ICE 网络连通（`18081` 仅供本机内部使用）

## 编译

```bash
npm ci
bash ./scripts/copy-novnc-core.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

使用 WSL2 开发：

```bash
cd /mnt/c/Users/Administrator/Projects/apache/remotelink
npm ci
bash ./scripts/copy-novnc-core.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## 配置

首次登录后，管理员在各协议的“管理”页面维护 RDP、VNC、SSH 连接，并在“用户管理”中分配连接权限。连接密码、SSH 私钥和 VNC CA 证书保存在 `REMOTELINK_STATE_DIR` 下仅服务账户可读的文件中，管理接口不会回显，并支持替换或清除。`REMOTELINK_TARGETS_FILE` 仅用于兼容导入旧版 RDP 目标，首次启动后会自动迁移为受管理连接。

| 变量 | 用途 |
| --- | --- |
| `REMOTELINK_TARGETS_FILE` | 可选的旧版 RDP 目标导入文件 |
| `REMOTELINK_ALLOWED_HOSTS` | 主机/CIDR 白名单；默认为 `*`（允许所有域名和 IP 地址） |
| `REMOTELINK_ACCESS_TOKEN_FILE` | 主管理员受保护的恢复凭据 |
| `REMOTELINK_TLS_CERTIFICATE` | TLS 证书链 |
| `REMOTELINK_TLS_PRIVATE_KEY` | TLS 私钥 |
| `REMOTELINK_STATE_DIR` | 用户、文件、打印任务和审计数据目录 |
| `REMOTELINK_USER_FILE_QUOTA_BYTES` | 可选的单用户文件配额 |
| `REMOTELINK_BLOCKED_FILE_EXTENSIONS` | 可选的上传扩展名黑名单 |

```bash
export REMOTELINK_TARGETS_FILE=/etc/remotelink/targets.json
export REMOTELINK_ALLOWED_HOSTS='*'
export REMOTELINK_ACCESS_TOKEN_FILE=/run/credentials/remotelink.service/access-token
export REMOTELINK_TLS_CERTIFICATE=/etc/remotelink/tls/fullchain.pem
export REMOTELINK_TLS_PRIVATE_KEY=/etc/remotelink/tls/privkey.pem
export REMOTELINK_STATE_DIR=/var/lib/remotelink
./build/remotelink
```

仅限本机开发时可使用 `REMOTELINK_ALLOW_INSECURE_HTTP=1`。局域网或互联网访问时不要使用不安全 HTTP。

## 部署

### Docker 镜像

执行脚本即可构建带版本号的本地镜像（默认读取 `VERSION`）：

```bash
bash ./scripts/build-docker.sh
REMOTELINK_ADMIN_PASSWORD='请修改为安全密码' docker compose up -d
```

服务监听 `18080` 端口，Compose 使用 `remotelink-state` 卷持久化用户、连接、凭据和审计数据。
默认配置假定 HTTPS 由可信反向代理终止，请勿把明文 HTTP 端口直接暴露到不受信任的网络。
如需指定镜像仓库、版本并通过 Buildx 推送：

```bash
REMOTELINK_IMAGE=ghcr.io/example/remotelink REMOTELINK_VERSION=0.3.1 \
  REMOTELINK_PUSH=1 bash ./scripts/build-docker.sh
```

分支推送和 Pull Request 会自动验证镜像构建；推送 `v*` 版本标签时，会同时将支持
`linux/amd64` 和 `linux/arm64` 的多架构版本镜像与 `latest` 发布到 GHCR 与 Docker Hub，
拉取时 Docker 会自动选择当前机器的架构。创建版本标签前，请在 GitHub 仓库 Secrets 中配置
`DOCKERHUB_USERNAME` 和 `DOCKERHUB_TOKEN`。Token 应使用具有读写权限的 Docker Hub
Personal Access Token，目标仓库为 `DOCKERHUB_USERNAME/remotelink`。
请先在 Docker Hub 中创建名为 `remotelink` 的仓库。

手工使用 Buildx 发布多架构镜像时，可同时设置
`REMOTELINK_PLATFORM=linux/amd64,linux/arm64` 和 `REMOTELINK_PUSH=1`。

### GitHub Actions 一键打包

打开 **Actions → Package RemoteLink for Linux → Run workflow**，即可构建并下载带版本号的
Linux x86_64 压缩包及 SHA-256 校验文件。推送 `v0.3.0` 这类版本标签时也会自动执行同一
打包流程。Actions 构建产物保留 30 天；创建 GitHub Release 仍是独立步骤。
发布包会把实际版本号写入 HTML 引用的 JavaScript 和样式地址。服务端要求 HTML
重新验证，同时对带版本号的静态资源启用一年不可变缓存，升级后不会继续使用旧前端文件。

在全新的 Ubuntu/Debian 主机上，可从仓库目录运行交互式一键安装脚本。脚本会安装
编译依赖、编译并测试 RemoteLink、创建加密凭据和自签名 TLS 证书，并启动 systemd
服务。再次运行时会更新程序文件，同时保留已有配置、凭据和用户数据。

```bash
sudo bash ./scripts/install-remotelink.sh
```

也可以不预先克隆仓库，直接通过 curl 一键安装：

```bash
curl -fsSL https://raw.githubusercontent.com/flydmonkey/RemoteLink/main/scripts/install-remotelink.sh | sudo bash
```

curl 安装方式会将当前 `main` 分支下载到临时目录，并直接从终端读取交互配置。

安装器会自动安装 FreeRDP 3 开发包；如果系统软件源没有提供，则会自动从 FreeRDP
官方源码仓库编译安装固定版本。systemd 版本仍需支持 `systemd-creds`。

如果没有配置证书，安装器会自动生成包含网关主机名和主要 IP 地址的自签名 TLS
证书。如需使用已有证书，可以通过环境变量同时传入证书和私钥路径：

```bash
sudo REMOTELINK_TLS_CERTIFICATE=/path/fullchain.pem \
  REMOTELINK_TLS_PRIVATE_KEY=/path/privkey.pem \
  bash ./scripts/install-remotelink.sh
```

如果 HTTPS/WSS 已由云厂商网关或 Nginx 终止，可以将 RemoteLink 安装为纯 HTTP
后端。此时 `18080` 只能对受信任的反向代理开放，不能直接暴露给用户：

```bash
curl -fsSL https://raw.githubusercontent.com/flydmonkey/RemoteLink/main/scripts/install-remotelink.sh \
  | sudo env REMOTELINK_TLS_MODE=off bash
```

反向代理需要同时转发普通 HTTP 请求以及 `/ws` 的 WebSocket Upgrade。

```nginx
location / {
    proxy_pass http://127.0.0.1:18080;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
}
```

如需手动部署或回滚：

```bash
sudo ./scripts/install-systemd.sh
sudo ./scripts/deploy-linux.sh
sudo ./scripts/rollback-linux.sh
./scripts/package-linux.sh
```

产品名称、可执行文件、系统服务、安装路径、配置变量和浏览器界面均统一使用 **RemoteLink**。

部署后访问 `https://网关地址:18080`。如果使用弱初始密码，请立即在“管理 → 用户管理”中修改。

## 开启 Windows RDP 硬件加速

为了改善拖动窗口和播放视频时的流畅度，建议在每台 Windows RDP 宿主机上开启硬件图形和 H.264 编码。请右键以下脚本并选择 **“以管理员身份运行”**：

```text
scripts\enable-windows-rdp-hardware-acceleration.bat
```

该脚本会：

- 为远程桌面会话启用硬件图形适配器；
- 启用 H.264 硬件编码；
- 优先使用 AVC 4:4:4 图形模式；
- 将桌面合成帧间隔设置为 15 毫秒；
- 刷新组策略。

执行完成后建议重启 Windows 宿主机，然后重新建立 RDP 连接。硬件加速是否真正生效仍取决于 GPU、显示驱动、Windows 版本和当前 RDP 策略。这里配置的是 Windows/RDP 宿主机侧加速；RemoteLink 网关自身的编码器偏好需要在网页设置中单独配置。

无法使用脚本时，可在管理员命令提示符中执行：

```bat
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services" /v UseHardwareGraphics /t REG_DWORD /d 1 /f
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services\H264Encoding" /v HardwareEncode /t REG_DWORD /d 1 /f
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services\H264Encoding" /v PrioritizeAVC444 /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations" /v DWMFRAMEINTERVAL /t REG_DWORD /d 15 /f
gpupdate /force
```

## 安全说明

- TLS 证书 SAN 必须包含网关主机名或 IP 地址。
- 安装后应立即修改初始管理员密码。
- 连接密码、SSH 私钥和 VNC CA 证书只保存在服务端，不会返回浏览器。
- 确认 SSH 信任前，应通过其他可信渠道核对 SHA-256 指纹；设备重装或主机密钥变化后需要重置信任并重新确认。
- 恢复令牌应保存在密码管理系统或 systemd 加密凭据中。
- `REMOTELINK_ALLOWED_HOSTS` 默认为 `*`，允许所有域名以及 IPv4/IPv6 地址；如部署策略需要限制目标，可设置明确的逗号分隔主机/CIDR 列表。
- 用户文件和打印任务按稳定用户身份隔离。
- 持久化用户注册表仅允许服务账户访问。

## 验证

```bash
curl --fail https://网关地址:18080/healthz
ctest --test-dir build --output-on-failure
npm run test:release
# 本地服务以及 Docker/Podman 测试目标可用时：
npm run test:e2e
```

`tests/` 目录还提供信令和 WebRTC 验证工具。

## 文档

- [架构](docs/architecture.md)
- [部署](docs/deployment.md)
- [验证](docs/verification.md)
- [浏览器界面设计](docs/winui-design.md)

## 项目状态

RemoteLink 仍在持续开发中。Linux 是当前支持的部署平台。在可信网络之外公开服务之前，请完整验证安全、网络、性能和恢复流程。
