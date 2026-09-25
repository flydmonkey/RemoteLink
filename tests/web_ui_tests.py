from pathlib import Path

root = Path(__file__).resolve().parents[1]
web = root / "web"
admin = (web / "admin.html").read_text(encoding="utf-8")
connect = (web / "connect.html").read_text(encoding="utf-8")
session = (web / "index.html").read_text(encoding="utf-8")
settings = (web / "settings.html").read_text(encoding="utf-8")
i18n = (web / "i18n.js").read_text(encoding="utf-8")

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
print("web-ui-tests-ok")
