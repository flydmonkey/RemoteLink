# RemoteLink project handoff

Last updated: 2026-09-26

This document is the starting point for the next maintainer. Read it together
with the English and Chinese README files, `CHANGELOG.md`, and
`docs/upgrade-0.3.md` before changing deployment or persistent state.

## Repository and current revision

- Repository: `git@github.com:flydmonkey/RemoteLink.git`
- Local checkout: `C:\Users\Administrator\Projects\apache\remote-gateway`
- Main branch: `main`
- Current handoff base: `f636569`
- Latest release tag: `v0.3.3`
- Version files currently contain `0.3.3`.
- The working tree was clean before this handoff document was added.

Important: `v0.3.3` points to `037a055`. The gzip response compression commit
`d958370` and favicon/runtime asset-version commit `f636569` were made after
that tag. Do not move or overwrite `v0.3.3`. Prepare `0.3.4` for the next
release if those changes need to be published as versioned Docker and Linux
artifacts.

## Product scope

RemoteLink is an independent browser-based remote access gateway. It does not
depend on Guacamole/guacd or a separate "guard" product.

- Frontend: plain HTML, CSS, and JavaScript; do not introduce Vue, React, or a
  heavy frontend framework.
- RDP: FreeRDP plus WebRTC H.264/Opus/data channels.
- VNC: noVNC Core only. Do not use the noVNC application UI. The server proxies
  one-time WSS tickets to RFB targets.
- SSH: xterm.js plus libssh2, password/private-key authentication, explicit
  host-fingerprint trust, and SFTP file management.
- Product name everywhere is `RemoteLink`; do not reintroduce
  `remote-gateway`, `Remote Gateway`, or `RG_*` names.

The UI has intentionally been normalized across RDP, VNC, and SSH. When
changing one connection page, check the other two for matching titles, icons,
avatar menus, button dimensions, labels, management navigation, and i18n.

## Implemented user experience

- Top protocol navigation for RDP, VNC, and SSH.
- Protocol tabs without authorized connections are hidden for standard users.
- Standard users can always open the avatar menu and sign out, including when
  they have no available connection.
- Avatar menus show the username only, use `Language`/`语言`, expose user
  management only to administrators, and use consistent styling.
- Each protocol has the same three management sections: Connection management,
  Active sessions, and Recent activity.
- Connection authorization is managed per user, grouped by RDP, VNC, and SSH.
- RDP credentials are configured on the managed connection, not entered on the
  connection page.
- VNC supports password, username/password, and CA-certificate modes with only
  the relevant credential fields displayed.
- VNC uses the local browser cursor and automatic scaling.
- SSH supports password or private-key credentials and hides fields belonging
  to the unselected authentication mode.
- RDP, VNC, and SSH session toolbars use the same side-toolbar style and the
  same disconnect-button treatment.
- English `My connections` labels remain on one line in all three tabs.
- Text responses larger than 1 KiB are gzip-compressed when the client accepts
  gzip. HTML, JavaScript, CSS, JSON, XML, and SVG are eligible; binary downloads
  and WebSockets are not.
- `/favicon.ico` serves a real multi-size ICO. The existing PNG icon remains at
  `/remotelink-icon.png`.
- HTML asset placeholders are replaced by the compiled project version at
  request time. Direct systemd/WSL deployments must return URLs such as
  `/i18n.js?v=0.3.3`, never `__REMOTELINK_VERSION__`.

## Important source locations

- `src/main.cpp`: application configuration, authentication, API routes, and
  protocol management.
- `src/http_server.cpp`: HTTP/TLS/WebSocket handling, static resources, cache
  headers, runtime asset versioning, favicon, and gzip negotiation.
- `src/session_manager.cpp`: RDP session lifecycle and auditing.
- `src/vnc_bridge.cpp`, `src/vnc_ticket_store.cpp`: VNC bridge and tickets.
- `src/ssh_bridge.cpp`, `src/ssh_files.cpp`: SSH terminal and SFTP operations.
- `web/connect.html`: RDP connection page and shared login behavior.
- `web/vnc.html`, `web/vnc-session.html`: VNC management and console.
- `web/ssh.html`, `web/ssh-session.html`: SSH management, terminal, and files.
- `web/admin.html`, `web/users.html`: RDP management and user administration.
- `web/i18n.js`: shared translations.
- `tests/browser/navigation.spec.mjs`: UI consistency and management tests.
- `tests/browser/session.spec.mjs`: RDP console tests.
- `tests/web_ui_tests.py`: static UI/branding/resource checks.
- `.github/workflows/docker-image.yml`: multi-architecture image verification
  and publication.
- `.github/workflows/package-linux.yml`: Linux release archive.

## Local build and test

The Windows checkout is built through Ubuntu WSL2. Native Node.js is not
installed inside that WSL distribution; invoking `npm` there resolves to the
Windows executable and fails. Run browser tests from PowerShell and CMake tests
inside WSL.

PowerShell browser tests:

```powershell
cd C:\Users\Administrator\Projects\apache\remote-gateway
npm ci
npx playwright test --project=chromium
```

WSL build and native tests:

```bash
cd /mnt/c/Users/Administrator/Projects/apache/remote-gateway
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc)"
ctest --test-dir build --output-on-failure
```

At handoff, all 16 Chromium tests and all 4 CTest tests passed. The build may
print harmless clock-skew warnings because the source tree is mounted from
Windows.

For the containerized protocol regression, use `npm run test:e2e`; see the
README for the optional test token/account environment variables. Never add
test or production credentials to the repository.

## WSL deployment state

Distribution: `Ubuntu` under WSL2.

- Active unit: `remotelink.service` (`active`, `enabled`).
- Old unit: `remote-gateway.service` (`inactive`, `disabled`).
- HTTPS endpoint: `https://localhost:18080`
- Health endpoint: `https://localhost:18080/healthz`
- Program root: `/opt/remotelink`
- Binary: `/opt/remotelink/bin/remotelink`
- Web root: `/opt/remotelink/web`
- Persistent state: `/var/lib/remotelink`
- Configuration and TLS: `/etc/remotelink`
- Encrypted systemd credentials: `/etc/credstore.encrypted`

The old deployment was migrated without deleting it. A full pre-migration
backup exists at:

```text
/opt/remote-gateway/backups/pre-remotelink-20260926070949
```

Additional timestamped backups exist under `/opt/remotelink/backups` and
`/opt/remote-gateway/backups`. Do not remove them without explicit approval.

For a normal source deployment after a successful build:

```bash
sudo install -m 0755 build/remotelink /opt/remotelink/bin/remotelink
sudo install -m 0644 build/libremotelink_streaming.so /opt/remotelink/lib/libremotelink_streaming.so
sudo cp -a web/. /opt/remotelink/web/
sudo systemctl restart remotelink
sudo systemctl is-active --quiet remotelink
curl -kfsS https://127.0.0.1:18080/healthz
```

Preserve `/var/lib/remotelink`, `/etc/remotelink`, and the encrypted credential
files during deployment. Before changing paths or service identities, inspect
the current unit with `systemctl cat remotelink` and take a backup.

Current deployment verification for the most recent changes:

```bash
curl -kfsS https://127.0.0.1:18080/favicon.ico -o /tmp/favicon.ico
curl -kfsS https://127.0.0.1:18080/ | grep 'i18n.js?v=0.3.3'
curl -kfsS --raw -H 'Accept-Encoding: gzip' -D - \
  'https://127.0.0.1:18080/i18n.js?v=0.3.3' -o /tmp/i18n.js.gz
gzip -t /tmp/i18n.js.gz
```

## Docker and release process

Published Docker Hub repository:

```text
flydmonkey/remotelink
```

Images are built for `linux/amd64` and `linux/arm64`; Docker selects the
matching architecture automatically. Docker Compose defaults to
`flydmonkey/remotelink:latest`. Quick-start commands are in both README files.

GitHub repository secrets already expected by the release workflow:

- `DOCKERHUB_USERNAME`
- `DOCKERHUB_TOKEN`

Never print, copy, or commit their values. A Docker Hub token was visible in an
earlier screenshot during setup; confirm that token has been revoked and
rotated if that has not already been done.

Release checklist:

1. Ensure `main` is clean and CI is green.
2. Choose the next version; the next version after this handoff should normally
   be `0.3.4` because `v0.3.3` already exists.
3. Update `VERSION`, `project(... VERSION ...)` in `CMakeLists.txt`,
   `package.json`, both version fields in `package-lock.json`, and
   `CHANGELOG.md`.
4. Run CTest, Playwright, and preferably the full release/e2e test.
5. Commit the release preparation.
6. Create an annotated tag, for example `git tag -a v0.3.4 -m "RemoteLink 0.3.4"`.
7. Push `main`, then push the tag.
8. Confirm the Docker and Linux package workflows succeed and verify the Docker
   manifest contains both AMD64 and ARM64.

Do not reuse or force-update a published tag.

## Security and deployment notes

- Credentials stay server-side and management APIs report only whether they
  are configured.
- VNC and SSH browser sessions use short-lived, single-use tickets.
- SSH host fingerprints require explicit administrator confirmation and can be
  reset through the UI.
- `REMOTELINK_BEHIND_TLS_PROXY=1` only permits an unencrypted HTTP/WebSocket
  backend because a trusted upstream proxy terminates HTTPS/WSS. It does not
  configure TLS or a proxy. When enabled, port `18080` must not be exposed
  directly to untrusted networks.
- `REMOTELINK_ALLOW_INSECURE_HTTP=1` is for localhost development only.
- Do not expose secrets in screenshots, logs, commits, issue text, or handoff
  documents.

## Immediate follow-up

1. Wait for or inspect the latest `main` CI runs triggered by `f636569`.
2. Confirm whether the Docker Hub token shown during setup was rotated.
3. Prepare `v0.3.4` when the gzip and favicon/runtime-version fixes are ready
   for a public release.
4. Before further UI work, use the existing Playwright screenshots/tests and
   compare RDP, VNC, and SSH together rather than adjusting one tab in
   isolation.
