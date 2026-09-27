import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
web = root / "web"
admin = (web / "admin.html").read_text(encoding="utf-8")
assert "#connection-backend" in admin and "width: 100%" in admin
connect = (web / "connect.html").read_text(encoding="utf-8")
session = (web / "index.html").read_text(encoding="utf-8")
settings = (web / "settings.html").read_text(encoding="utf-8")
i18n = (web / "i18n.js").read_text(encoding="utf-8")
vnc_session = (web / "vnc-session.html").read_text(encoding="utf-8")
ssh_session = (web / "ssh-session.html").read_text(encoding="utf-8")
guacamole_session = (web / "guacamole-session.html").read_text(encoding="utf-8")
localized_pages = [
    web / name
    for name in (
        "connect.html",
        "admin.html",
        "settings.html",
        "index.html",
        "vnc.html",
        "vnc-session.html",
        "ssh.html",
        "ssh-session.html",
        "guacamole-session.html",
        "users.html",
    )
]

favicon = (web / "favicon.ico").read_bytes()
assert favicon[:4] == b"\x00\x00\x01\x00", "favicon.ico is missing or invalid"

for native_dialog in ("prompt(", "confirm(", "alert("):
    assert native_dialog not in "\n".join((admin, connect, session, settings))

assert "connectionSearch" in admin and "connectionSort" in admin
assert "/api/admin/connections/update" in admin
assert "/api/admin/connections/delete" in admin
for field in ("visual-preset", "wallpaper", "max-fps", "guacamole-format",
              "guacamole-resize", "clipboard"):
    assert f'id="{field}"' in settings, f"connection settings are missing {field}"
assert 'id="connection-video-bitrate"' not in admin
assert "selected.bitrate" in connect and "selected.sound" in connect
for option in ("maxFps", "wallpaper", "fontSmoothing", "fullWindowDrag",
               "menuAnimations", "desktopComposition", "clipboard"):
    assert option in session, f"FreeRDP session payload is missing {option}"
for option in ("guacamoleFormat", "guacamoleDpi", "guacamoleResize", "clipboard"):
    assert option in guacamole_session, f"Guacamole session payload is missing {option}"
assert 'id="encoder"' not in settings
assert 'id="files"' in settings and "files:files.checked" in settings
assert "files: sessionOptions.files" in session
assert "redirect_files" in (root / "src" / "session_manager.cpp").read_text(encoding="utf-8")
assert "passwordFile" in (root / "src" / "main.cpp").read_text(encoding="utf-8")
assert '"password", target.rdp.password' not in (root / "src" / "main.cpp").read_text(encoding="utf-8")
for language in ("zh-TW", "en", "ja", "ko"):
    assert language in i18n
    assert language in session
assert "requestVideoFrameCallback" in session
assert "whiteRatio" in session
assert "function translationRoot" in i18n
assert "function translationRoot" in session
for source, button_id in (
    (session, "logout"),
    (vnc_session, "disconnect"),
    (ssh_session, "disconnect"),
    (guacamole_session, "disconnect"),
):
    button = re.search(
        rf"<button\b(?=[^>]*\bid=\"{button_id}\")[^>]*>", source, re.DOTALL
    ).group(0)
    assert 'class="disconnect-action"' in button
    assert 'aria-label="断开连接"' in button
    assert 'class="danger"' not in button
for feature in ("sessionSize", "createClipboardStream", "StringReader", "requestFullscreen", "0xFFFF",
                "onfilesystem", "onfile", "/api/admin/files", "/api/admin/prints"):
    assert feature in guacamole_session, f"Guacamole session is missing {feature}"
assert "sendAck(stream.index" in guacamole_session, "Guacamole input streams must be acknowledged"
assert "sendMouseState(state,true)" in guacamole_session
assert "state.x/scale" not in guacamole_session and "state.y/scale" not in guacamole_session
http_server = (root / "src" / "http_server.cpp").read_text(encoding="utf-8")
for parameter in ("enable-printing", "enable-drive", "drive-path", "RemoteLink Printer",
                  "enable-wallpaper", "enable-font-smoothing", "disable-copy", "resize-method"):
    assert parameter in http_server, f"Guacamole handshake is missing {parameter}"
assert "download_streams" in http_server and http_server.count(
    '{"ack", (*instruction)[1], "OK", "0"}'
) == 1, "Only the initial Guacamole file instruction may be ACKed by the gateway"
assert "duplicate ACKs" in http_server
readme_zh = (root / "README.zh-CN.md").read_text(encoding="utf-8")
assert "无需为 Guacamole 开放 UDP 端口" in readme_zh
assert "无需为 guacd 单独申请、安装或配置 TLS 证书" in readme_zh
compose = (root / "docker-compose.yml").read_text(encoding="utf-8")
dockerfile = (root / "Dockerfile").read_text(encoding="utf-8")
docker_workflow_path = root / ".github" / "workflows" / "docker-image.yml"
docker_workflow = docker_workflow_path.read_text(encoding="utf-8") if docker_workflow_path.exists() else ""
guacd_unit = (root / "deploy" / "remotelink-guacd.service").read_text(encoding="utf-8")
assert "REMOTELINK_GUACD_HOST: 127.0.0.1" in compose
assert "guacd libguac-client-rdp0t64" in dockerfile
assert "ghostscript" in dockerfile
assert "ghostscript podman" in (
    root / "scripts" / "install-remotelink.sh").read_text(encoding="utf-8")
assert "print-smoke.pdf" in docker_workflow if docker_workflow else True
assert "if (options_.redirect_printers && !options_.print_jobs_path.empty())" in (
    root / "src" / "rdp_frame_source.cpp").read_text(encoding="utf-8")
assert "&& user_identity == 0" not in (
    root / "src" / "session_manager.cpp").read_text(encoding="utf-8")
assert "/usr/sbin/guacd -f -b 127.0.0.1" in (root / "docker" / "entrypoint.sh").read_text(encoding="utf-8")
assert "ctest --test-dir build --output-on-failure" in dockerfile
if docker_workflow:
    assert "remotelink-guacd" in docker_workflow and "sbom: true" in docker_workflow
assert "@REMOTELINK_UID@:@REMOTELINK_GID@" in guacd_unit
for page in localized_pages:
    source = page.read_text(encoding="utf-8")
    assert "/i18n.js" in source, f"{page.name} is missing shared i18n"
    for old_brand in ("Remote" + " Gateway", "remote" + "-gateway", "remote" + "_gateway"):
        assert old_brand not in source, f"{page.name} contains the old brand"
for retired_copy in ("连接历史", "VNC 管理", "SSH 设置", "SSH 管理"):
    assert retired_copy not in "\n".join(
        page.read_text(encoding="utf-8") for page in localized_pages
    )
print("web-ui-tests-ok")
