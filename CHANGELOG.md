# RemoteLink changelog

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
