# RemoteLink

[简体中文](README.zh-CN.md) | English

RemoteLink is a self-hosted, browser-based remote connection tool for RDP, VNC, and SSH. It supports server-managed SSH password and private-key authentication without exposing credentials to the browser.

The gateway runs as a single Linux service. Users only need a modern browser—no extension or desktop client is required.

## Features

- Low-latency H.264 remote desktop streaming and Opus audio over WebRTC
- Keyboard, mouse, wheel, touch, Unicode paste, fullscreen, and secure-attention controls
- Multiple configured RDP targets protected by an explicit host allow-list
- Username/password accounts with administrator and standard-user roles
- User creation, enable/disable, and password reset in the administration console
- Administrator-managed connections with per-user authorization
- Standard users see only authorized connections; protocol tabs with no available connections are hidden
- Per-user file manager with folders, chunked uploads, downloads, rename, delete, quotas, and audit logs
- RDP drive redirection through **Gateway Files**
- Printer redirection with downloadable PDF print jobs
- Live frame rate, bitrate, resolution, codec, audio, drop, and traffic statistics
- Administrator dashboard for service health, sessions, events, and forced disconnects
- Standard users cannot access administrator status or disconnect operations
- English, Simplified Chinese, Traditional Chinese, Japanese, and Korean interfaces
- System-language detection with a persistent manual override
- Configurable resolution, bitrate, audio, printing, and experimental hardware acceleration
- HTTPS/WSS, encrypted systemd credentials, and isolated per-user storage
- Responsive desktop and mobile browser interface
- Standalone VNC support using noVNC Core and a one-time-ticket WSS-to-RFB proxy
- Native RemoteLink VNC device, session toolbar, administration, and user-permission UI
- VNC password, username/password, and CA-certificate authentication modes with automatic reconnect
- Native SSH terminal with password or private-key credentials and administrator-confirmed host fingerprints
- Unified RDP, VNC, and SSH active-session and recent-activity audit views
- Non-revealing credential status with replace and clear operations for every protocol

## Architecture

```text
Browser (plain HTML/CSS/JavaScript)
  ├─ RDP: WSS signaling + WebRTC H.264/Opus/data ── RemoteLink ── FreeRDP ── Windows
  ├─ VNC: one-time-ticket WSS ───────────────────── RemoteLink ── TCP/RFB ─── VNC server
  └─ SSH: one-time-ticket WSS + xterm.js ────────── RemoteLink ── libssh2 ─── SSH server
```

The VNC client uses only noVNC Core; RemoteLink supplies its own connection and session interface. VNC and SSH credentials remain on the server, and browsers receive only short-lived, single-use session tickets. Each RDP browser connection owns an isolated capture, encoder, audio, and input lifecycle; video and input are never broadcast between users.

## Requirements

- Linux (the maintained deployment target)
- CMake 3.20+ and a C++20 compiler
- FreeRDP 3 and WinPR 3 development packages
- OpenH264, Opus, libyuv, OpenSSL, libssh2, and libdatachannel
- A TLS certificate trusted by client browsers
- TCP 18080 and WebRTC UDP ICE connectivity (`18081` is loopback-only internally)

## Build

```bash
npm ci
bash ./scripts/copy-novnc-core.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For WSL2 development:

```bash
cd /mnt/c/Users/Administrator/Projects/apache/remote-gateway
npm ci
bash ./scripts/copy-novnc-core.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Configuration

After first login, administrators manage RDP, VNC, and SSH connections from each protocol's **Management** page and grant access from **User management**. Connection credentials are stored in owner-only files under `RG_STATE_DIR`, are never returned by the API, and can be replaced or cleared from the interface. `RG_TARGETS_FILE` remains available for importing legacy RDP targets; imported connections are migrated to managed state on first start.

| Variable | Purpose |
| --- | --- |
| `RG_TARGETS_FILE` | Optional legacy RDP target import file |
| `RG_ALLOWED_HOSTS` | RDP host/CIDR allow-list; defaults to `*` (all DNS names and IP addresses). VNC destinations are unrestricted. |
| `RG_ACCESS_TOKEN_FILE` | Protected primary-administrator recovery credential |
| `RG_TLS_CERTIFICATE` | TLS certificate chain |
| `RG_TLS_PRIVATE_KEY` | TLS private key |
| `RG_STATE_DIR` | Persistent users, files, print jobs, and audit state |
| `RG_USER_FILE_QUOTA_BYTES` | Optional per-user file quota |
| `RG_BLOCKED_FILE_EXTENSIONS` | Optional blocked upload extensions |

```bash
export RG_TARGETS_FILE=/etc/remote-gateway/targets.json
export RG_ALLOWED_HOSTS='*'
export RG_ACCESS_TOKEN_FILE=/run/credentials/remote-gateway.service/access-token
export RG_TLS_CERTIFICATE=/etc/remote-gateway/tls/fullchain.pem
export RG_TLS_PRIVATE_KEY=/etc/remote-gateway/tls/privkey.pem
export RG_STATE_DIR=/var/lib/remote-gateway
./build/remote-gateway
```

`RG_ALLOW_INSECURE_HTTP=1` may be used for localhost-only development. Never use insecure HTTP for LAN or Internet access.

## Deployment

### GitHub Actions package

Open **Actions → Package RemoteLink for Linux → Run workflow** to build and
download a versioned Linux x86_64 archive and its SHA-256 checksum. Pushing a
tag such as `v0.3.0` runs the same packaging workflow automatically. The workflow
artifact is retained for 30 days; creating a GitHub Release is a separate step.
Packaged HTML references JavaScript and styles with the package version. The
server keeps HTML revalidated while serving versioned static assets with a
one-year immutable cache, so upgrades do not reuse stale browser code.

For a fresh Ubuntu/Debian host, run the interactive one-click installer from the
repository checkout. It installs build dependencies, builds and tests RemoteLink,
creates encrypted credentials and a self-signed TLS certificate, and starts the
systemd service. Running it again updates the binaries while preserving existing
configuration, credentials, and user data.

```bash
sudo bash ./scripts/install-remotelink.sh
```

Or install directly with curl without cloning the repository first:

```bash
curl -fsSL https://raw.githubusercontent.com/flydmonkey/RemoteLink/main/scripts/install-remotelink.sh | sudo bash
```

The curl installer downloads the current `main` branch into a temporary directory
and uses the terminal for its interactive prompts.

The installer installs FreeRDP 3 development packages automatically. If the
distribution does not provide them, it builds a pinned FreeRDP 3 release from
the official source repository. A systemd version with `systemd-creds` support
is still required.

If no certificate is configured, the installer generates a self-signed TLS
certificate containing the gateway hostname and primary IP address. To install
an existing certificate instead, pass both paths through the environment:

```bash
sudo REMOTELINK_TLS_CERTIFICATE=/path/fullchain.pem \
  REMOTELINK_TLS_PRIVATE_KEY=/path/privkey.pem \
  bash ./scripts/install-remotelink.sh
```

When HTTPS/WSS is terminated by a cloud gateway or Nginx, install RemoteLink in
plain-HTTP backend mode. Port `18080` must then be reachable only by the trusted
proxy, not exposed directly to users:

```bash
curl -fsSL https://raw.githubusercontent.com/flydmonkey/RemoteLink/main/scripts/install-remotelink.sh \
  | sudo env REMOTELINK_TLS_MODE=off bash
```

The proxy must forward normal HTTP requests and WebSocket upgrades for `/ws`.

```nginx
location / {
    proxy_pass http://127.0.0.1:18080;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
}
```

For manual deployment and rollback:

```bash
sudo ./scripts/install-systemd.sh
sudo ./scripts/deploy-linux.sh
sudo ./scripts/rollback-linux.sh
./scripts/package-linux.sh
```

See [the 0.3 upgrade guide](docs/upgrade-0.3.md) before upgrading an existing
0.2 installation. Release changes are recorded in [CHANGELOG.md](CHANGELOG.md).

Run `npm run test:e2e` against a local installed service to start the RDP, VNC,
and SSH containers and execute the full regression. It uses the bootstrap
`admin` / `admin` account only when no token is supplied; set
`REMOTELINK_TEST_TOKEN`, or `REMOTELINK_TEST_ADMIN_USERNAME` and
`REMOTELINK_TEST_ADMIN_PASSWORD`, after changing that credential.

The deployment retains the internal `remote-gateway` service identifier for compatibility, while the product and browser interface use **RemoteLink**.

Open `https://GATEWAY-IP:18080` after deployment. Change weak bootstrap credentials immediately under **Administration → User management**.

## Enable Windows RDP hardware acceleration

For smoother window movement and video playback, enable hardware graphics and H.264 encoding on each Windows RDP host. Run the included script **as Administrator**:

```text
scripts\enable-windows-rdp-hardware-acceleration.bat
```

The script applies these host-side policies:

- use hardware graphics adapters for Remote Desktop sessions;
- enable hardware H.264 encoding;
- prioritize AVC 4:4:4 graphics mode;
- set the desktop composition frame interval to 15 ms;
- refresh Group Policy.

Restart the Windows host and reconnect the RDP session after running it. Hardware acceleration still depends on a supported GPU, a working display driver, the Windows edition, and the active RDP policy. This setting accelerates the Windows/RDP host side; RemoteLink's gateway encoder preference is configured separately in the web settings.

Manual commands, if the script cannot be used:

```bat
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services" /v UseHardwareGraphics /t REG_DWORD /d 1 /f
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services\H264Encoding" /v HardwareEncode /t REG_DWORD /d 1 /f
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services\H264Encoding" /v PrioritizeAVC444 /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations" /v DWMFRAMEINTERVAL /t REG_DWORD /d 15 /f
gpupdate /force
```

## Security

- The certificate SAN must contain the gateway hostname or IP address.
- Change the bootstrap administrator password immediately after installation.
- Connection passwords, SSH private keys and VNC CA certificates are stored server-side and never returned to browsers.
- Verify an SSH SHA-256 host fingerprint out of band before confirming trust; reset and confirm it again after a host reinstall or key change.
- Keep the recovery token in a secret manager or systemd encrypted credentials.
- `RG_ALLOWED_HOSTS` defaults to `*`, allowing every domain name and IPv4/IPv6 address. Set an explicit comma-separated host/CIDR list when deployment policy requires destination restrictions.
- User files and print jobs are isolated by stable user identity.
- The persistent user registry uses owner-only permissions.

## Verification

```bash
curl --fail https://GATEWAY-IP:18080/healthz
ctest --test-dir build --output-on-failure
npm run test:release
# With Docker or Podman test targets and a local installed service:
npm run test:e2e
```

Additional signaling and WebRTC smoke clients are available under `tests/`.

## Documentation

- [Architecture](docs/architecture.md)
- [Deployment](docs/deployment.md)
- [Verification](docs/verification.md)
- [Browser UI design](docs/winui-design.md)

## Project status

RemoteLink is under active development. Linux is the supported deployment platform. Validate security, networking, performance, and recovery procedures before exposing it outside a trusted network.
