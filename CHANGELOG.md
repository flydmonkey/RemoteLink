# RemoteLink changelog

## 0.3.22

- Translate RDP connection test results and connection saved notifications as complete phrases.
- Preserve administrator-provided connection names as user data instead of translating them.

## 0.3.21

- Eliminate mixed Chinese and English in RDP, VNC, and SSH editing and destructive-action flows.
- Localize persisted activity messages and disconnect reasons as complete phrases.
- Replace native localized validation bubbles with RemoteLink inline validation feedback.
- Isolate shared dialog layout from page-specific dialog CSS.

## 0.3.20

- Replace native browser confirmation and prompt dialogs with localized RemoteLink-styled dialogs.
- Use a distinct danger action for destructive operations and support keyboard and backdrop cancellation.
- Migrate connection deletion, credential clearing, SSH trust reset, file deletion, and folder creation flows.

## 0.3.19

- Translate dynamic connection errors, credential summaries, and native browser dialogs.
- Translate embedded status fragments used by administration, VNC, Telnet, and Guacamole interaction branches.
- Extend i18n regression coverage to source literals and interactive Telnet failure flows.

## 0.3.18

- Complete the visible-copy i18n coverage across administration, settings, RDP, VNC, SSH, Telnet, and user-management modules.
- Support parameterized translations for timestamps, automatic display dimensions, and volume status.
- Add browser audits that prevent untranslated Chinese copy and incomplete locale coverage from regressing.

## 0.3.17

- Include Telnet targets and sessions in the unified administrator status and activity views.
- Add Telnet interface translations and browser regression coverage for connection management and semantic feedback.
- Remove inherited SSH fingerprint, private-key, and SFTP code from the Telnet pages.

## 0.3.16

- Add managed Telnet connections with optional server-side login credentials, per-user authorization, activity auditing, and administrator disconnect controls.
- Add a browser-based Telnet terminal using single-use WebSocket tickets and Telnet option negotiation filtering.
- Add Telnet navigation, management, and differentiated informational, success, and error feedback.

## 0.3.15

- Bundle guacd 1.3.0 as the default Guacamole RDP backend for better compatibility with legacy Windows applications.
- Keep the native RemoteLink RDP backend on the unmodified system FreeRDP 3.31.0 stack for modern Windows hosts.
- Accept large file-upload request bodies and remove file preview actions that browsers handled as downloads.
- Document Guacamole printing of blank PDFs as a known legacy-backend limitation.

## 0.3.14

- Use Guacamole's standard `BlobReader` print flow: acknowledge stream creation once, acknowledge each PDF block once, and upload the completed PDF through RemoteLink's authenticated per-user print endpoint.
- Keep the Guacamole WebSocket proxy transparent and make the managed guacd log level configurable with `REMOTELINK_GUACD_LOG_LEVEL`.

## 0.3.13

- Terminate Guacamole print-PDF data streams at the gateway so printing no longer depends on browser ACK timing and repeated jobs cannot stall behind the WebSocket.

## 0.3.12

- Prevent duplicate Guacamole print-stream acknowledgements that could create an empty first PDF and block later print jobs.

## 0.3.11

- Restore the session-aware FreeRDP printer so every authenticated user receives PDFs in their own print directory.
- Install Ghostscript in Docker and bare-metal deployments so FreeRDP and Guacamole print streams are converted into non-empty PDF files.
- Exercise PostScript-to-PDF conversion during the Docker image smoke test.

## 0.3.10

- Keep Docker image tests compatible with the intentionally reduced build context while retaining workflow-policy checks in repository CI.

## 0.3.9

- Add a selectable Guacamole RDP backend for older Windows hosts while retaining the restored FreeRDP path for modern Windows.
- Add shared connection display/resource settings plus backend-specific FreeRDP bitrate/frame-rate and Guacamole image/DPI/resize settings.
- Integrate Guacamole printing, per-user file management, clipboard, audio, resolution handling, and corrected pointer scaling.
- Isolate files and print jobs by RemoteLink user and restore FreeRDP printing for the primary administrator.
- Bundle guacd in the official container image as a separately supervised loopback-only process; bare-metal installs continue to use a separate systemd service.
- Extend Docker and GitHub Actions validation with container tests, guacd health checks, SBOM generation, provenance, and multi-architecture release publishing.
- Document that Guacamole needs no exposed UDP range and no separate guacd TLS certificate.

## 0.3.8

- Recreate the H.264 encoder when the Windows desktop size differs from the requested resolution.
- Draw 1-bit and 8-bit cursors sent by older Windows hosts.
- Use 1920×1080 when connection display settings have not been saved.
- Require a username and password when creating an RDP connection.

## 0.3.7

- Allow outbound RDP connections to negotiate TLS 1.0 and SHA-1 certificates. The gateway HTTPS listener still requires TLS 1.2 or newer.

## 0.3.6

- Fix the release build so the Docker image serves HTTPS by default. Tag `v0.3.5` did not publish because `tls_enabled` was left undeclared.

## 0.3.5

- Ship a self-signed TLS certificate in the Docker image at `/etc/remotelink/tls/fullchain.pem` and `/etc/remotelink/tls/privkey.pem`.
- Serve plain HTTP when `REMOTELINK_BEHIND_TLS_PROXY=1`, and HTTPS with that certificate otherwise.

## 0.3.4

- Advertise a configured host address on ICE candidates so browsers outside a Docker network can receive RDP media.
- Compress eligible HTTP text responses with gzip when the client accepts gzip.
- Serve a multi-size favicon and stamp HTML asset URLs with the running version.
- Ignore detached text nodes while translating session pages.

## 0.3.3

- Added Docker Hub quick-start documentation for the published AMD64 and ARM64 images.
- Documented the security behavior and deployment requirements of TLS proxy mode.
- Kept English connection labels on one line across RDP, VNC, and SSH.
- Simplified account menus to show only the username and use the shared Language label.

## 0.3.2

- Publish Docker release images for both AMD64 and ARM64 under the same version tags.

## 0.3.1

- Unified all product, service, executable, configuration, and container naming as RemoteLink.
- Added reproducible Docker image packaging with persistent state and health checks.
- Added release publishing to GHCR and Docker Hub through GitHub Actions.

## 0.3.0

- Added unified RDP, VNC, and SSH session auditing with user, target, duration,
  and disconnect-reason details.
- Added administrator disconnect controls for all three protocols in the
  central management console.
- Added explicit VNC authentication modes and mutually exclusive credential
  storage.
- Added replace/clear operations and non-revealing credential status for RDP,
  VNC, and SSH.
- Changed SSH host-key onboarding to require explicit administrator
  confirmation after a successful fingerprint probe.
- Added automatic VNC reconnection using the RDP backoff policy.
- Added a one-command container regression for RDP, VNC, and SSH.
- Standardized connection, management, and session controls across protocols.

## 0.2.0

- Added standalone VNC and SSH connections, administration, authorization,
  and browser sessions.
- Added server-managed users and per-target authorization.
