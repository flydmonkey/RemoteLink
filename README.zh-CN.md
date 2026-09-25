# RemoteLink

简体中文 | [English](README.md)

RemoteLink 是一款可自行部署、通过浏览器使用的远程连接工具，用于通过 RDP 访问 Windows 桌面。它使用 FreeRDP 捕获远程画面，通过 WebRTC 传输 H.264 视频和 Opus 音频，并通过 WebRTC 数据通道发送键盘、鼠标、滚轮、触摸和文本输入。

网关以单个 Linux 服务运行。用户只需要现代浏览器，不需要浏览器扩展或桌面客户端。

## 功能

- 基于 WebRTC 的低延迟 H.264 画面和 Opus 声音
- 键盘、鼠标、滚轮、触摸、Unicode 文本粘贴、全屏和安全界面操作
- 多个预配置 RDP 目标，并通过主机白名单限制访问范围
- 用户名和密码登录，区分管理员与普通用户
- 在管理控制台创建、启停用户和重置密码
- 管理员统一添加连接，并按用户授权可访问的连接
- 普通用户只显示获授权连接，只能配置本次会话参数
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

## 架构

```text
浏览器
  ├─ HTTPS 页面和认证接口                      :18080
  └─ WSS 信令 `/ws` 以及 WebRTC 媒体/数据       :18080
                         │
                    RemoteLink
              ┌──────────┴──────────┐
              │ FreeRDP 会话        │
              │ H.264 / Opus        │
              │ 输入转发            │
              │ 文件 / PDF 打印     │
              └──────────┬──────────┘
                         │ RDP
                    Windows 主机
```

每个浏览器连接拥有独立的 RDP、画面捕获、编码器、声音和输入生命周期。不同用户之间不会广播画面或输入。

## 环境要求

- Linux（当前维护的部署平台）
- CMake 3.20 或更高版本，以及支持 C++20 的编译器
- FreeRDP 3 和 WinPR 3 开发包
- OpenH264、Opus、libyuv、OpenSSL 和 libdatachannel
- 客户端浏览器信任的 TLS 证书
- TCP 18080 以及 WebRTC UDP ICE 网络连通（`18081` 仅供本机内部使用）

## 编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

使用 WSL2 开发：

```bash
cd /mnt/c/Users/Administrator/Projects/apache/remote-gateway
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## 配置

将 `config/targets.example.json` 复制到仓库之外的受保护目录，然后配置允许连接的 Windows 目标。密码应通过环境变量或 systemd 加密凭据提供，不要直接存入 JSON。

| 变量 | 用途 |
| --- | --- |
| `RG_TARGETS_FILE` | RDP 目标配置文件 |
| `RG_ALLOWED_HOSTS` | 主机/CIDR 白名单；默认为 `*`（允许所有域名和 IP 地址） |
| `RG_ACCESS_TOKEN_FILE` | 主管理员受保护的恢复凭据 |
| `RG_TLS_CERTIFICATE` | TLS 证书链 |
| `RG_TLS_PRIVATE_KEY` | TLS 私钥 |
| `RG_STATE_DIR` | 用户、文件、打印任务和审计数据目录 |
| `RG_USER_FILE_QUOTA_BYTES` | 可选的单用户文件配额 |
| `RG_BLOCKED_FILE_EXTENSIONS` | 可选的上传扩展名黑名单 |

```bash
export RG_TARGETS_FILE=/etc/remote-gateway/targets.json
export RG_ALLOWED_HOSTS='*'
export RG_ACCESS_TOKEN_FILE=/run/credentials/remote-gateway.service/access-token
export RG_TLS_CERTIFICATE=/etc/remote-gateway/tls/fullchain.pem
export RG_TLS_PRIVATE_KEY=/etc/remote-gateway/tls/privkey.pem
export RG_STATE_DIR=/var/lib/remote-gateway
./build/remote-gateway
```

仅限本机开发时可使用 `RG_ALLOW_INSECURE_HTTP=1`。局域网或互联网访问时不要使用不安全 HTTP。

## 部署

### GitHub Actions 一键打包

打开 **Actions → Package RemoteLink for Linux → Run workflow**，即可构建并下载带版本号的
Linux x86_64 压缩包及 SHA-256 校验文件。推送 `v0.1.0` 这类版本标签时也会自动执行同一
打包流程。

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

为兼容已有安装，部署仍保留内部服务标识 `remote-gateway`；产品名称和浏览器界面使用 **RemoteLink**。

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
- RDP 密码和恢复令牌应保存在密码管理系统或 systemd 加密凭据中。
- `RG_ALLOWED_HOSTS` 默认为 `*`，允许所有域名以及 IPv4/IPv6 地址；如部署策略需要限制目标，可设置明确的逗号分隔主机/CIDR 列表。
- 用户文件和打印任务按稳定用户身份隔离。
- 持久化用户注册表仅允许服务账户访问。

## 验证

```bash
curl --fail https://网关地址:18080/healthz
ctest --test-dir build --output-on-failure
```

`tests/` 目录还提供信令和 WebRTC 验证工具。

## 文档

- [架构](docs/architecture.md)
- [部署](docs/deployment.md)
- [验证](docs/verification.md)
- [浏览器界面设计](docs/winui-design.md)

## 项目状态

RemoteLink 仍在持续开发中。Linux 是当前支持的部署平台。在可信网络之外公开服务之前，请完整验证安全、网络、性能和恢复流程。
