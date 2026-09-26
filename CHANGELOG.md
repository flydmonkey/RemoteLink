# RemoteLink changelog

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
