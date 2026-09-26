# Upgrading RemoteLink 0.2 to 0.3

RemoteLink 0.3 keeps the state file format and does not require a database
conversion. Back up `REMOTELINK_STATE_DIR` and the service configuration before
deploying the new package. Installations created before the product-wide naming
cleanup must move their state and configuration into the documented RemoteLink
paths and update environment-variable names to the `REMOTELINK_` prefix.

## Automatic compatibility

- Existing RDP, VNC, SSH, user, and authorization files remain valid.
- Existing VNC credentials are classified as CA certificate, username and
  password, or password-only when first edited. Saving the connection removes
  credentials that do not belong to the selected mode.
- Existing trusted SSH fingerprints remain trusted.
- Existing VNC and SSH activity entries remain readable. Older history entries
  have no disconnect reason; new entries record one.

## SSH trust change

New SSH connections are no longer trusted automatically. Save the connection,
run **Test connection**, verify the displayed SHA-256 fingerprint against the
target administrator's value, and select **Confirm and trust fingerprint**.
Resetting trust blocks new SSH sessions until this process is repeated.

## Credentials

Connection lists only report whether credentials are configured. Passwords,
private keys, passphrases, and certificates are never returned by management
APIs. Clearing credentials makes subsequent connections fail until an
administrator supplies replacements.

## Verification and rollback

Run `npm run test:release` before deployment. With the local protocol test
stack and RemoteLink service available, run `npm run test:e2e`. To roll back a
systemd deployment, use `sudo scripts/rollback-linux.sh`; the 0.3 state files
remain readable by 0.3 releases, while a 0.2 rollback will ignore newly added
audit fields.
