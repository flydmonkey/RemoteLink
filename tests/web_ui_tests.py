import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
web = root / "web"
admin = (web / "admin.html").read_text(encoding="utf-8")
connect = (web / "connect.html").read_text(encoding="utf-8")
session = (web / "index.html").read_text(encoding="utf-8")
settings = (web / "settings.html").read_text(encoding="utf-8")
i18n = (web / "i18n.js").read_text(encoding="utf-8")
vnc_session = (web / "vnc-session.html").read_text(encoding="utf-8")
ssh_session = (web / "ssh-session.html").read_text(encoding="utf-8")
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
for source, button_id in (
    (session, "logout"),
    (vnc_session, "disconnect"),
    (ssh_session, "disconnect"),
):
    button = re.search(
        rf"<button\b(?=[^>]*\bid=\"{button_id}\")[^>]*>", source, re.DOTALL
    ).group(0)
    assert 'class="disconnect-action"' in button
    assert 'aria-label="断开连接"' in button
    assert 'class="danger"' not in button
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
