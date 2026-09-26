# LAN deployment

## Security model

The browser authenticates to the gateway with `REMOTELINK_ACCESS_TOKEN`. The token is
sent only over WSS and is kept in page memory. RDP usernames and passwords are
read by the service process and never appear in HTML, JavaScript, URLs, or
signaling messages.

`REMOTELINK_ALLOWED_HOSTS` defaults to `*`, which permits any non-empty DNS name,
IPv4 address, or IPv6 address. It can instead be a comma-separated list of
exact host names, exact IP addresses, and IPv4 CIDRs. Every host in the JSON
file named by `REMOTELINK_TARGETS_FILE` must match that policy or the process refuses to
start. Target entries contain `passwordEnv`, not a password; the referenced
environment variable must exist at startup. Target IDs must be unique and use
only letters, digits, `_`, and `-`.

For a persistent systemd deployment, prefer encrypted systemd credentials over
plain environment files. `config/remotelink.service.example` loads an
encrypted gateway token and RDP password into `/run/credentials`; the matching
`config/targets.systemd.example.json` uses `passwordFile`. The service also
accepts `REMOTELINK_ACCESS_TOKEN_FILE` for the mounted gateway-token credential. Never
commit encrypted credentials: machine-bound encryption protects them at rest,
but they remain deployment secrets.

After authentication, signaling exposes only each target's ID and display
name. Selecting a target creates a dedicated FreeRDP connection, H.264 encoder,
video track, and input channel for that browser peer. Closing that peer stops
only its own session. A case-insensitive host and Windows username pair can
have only one active session; a second peer receives `target-user-busy` before
an RDP connection is attempted. Different hosts or usernames remain independent.

## Certificate

Use a certificate issued by an internal CA already trusted by LAN clients, or a
publicly trusted certificate for an internal DNS name. The certificate SAN must
contain every gateway hostname or IP clients use. The public HTTPS listener
uses the PEM certificate and private key configured with `REMOTELINK_TLS_CERTIFICATE`
and `REMOTELINK_TLS_PRIVATE_KEY`; its `/ws` route carries WebSocket signaling.

A self-signed certificate is acceptable for isolated testing only. Importing a
self-signed root into every client is an administrator decision; the service
does not alter client trust stores or bypass browser certificate warnings.

For a local automated smoke test only, `https://localhost:18080/#token=TOKEN`
may be used. The fragment is not transmitted in the HTTP request, is accepted
only on `localhost`/`127.0.0.1`, and is removed from the address bar immediately.
Do not share or bookmark a URL containing a token.

Remove an expired test certificate from the current Windows user trust store
with `certutil -user -delstore Root CERTIFICATE_THUMBPRINT`. Production
deployments should use an internal or public CA and normal certificate renewal.

## Network

- TCP 18080: HTTPS UI, REST API, health endpoint, and WSS signaling at `/ws`.
- TCP 18081 on loopback only: internal plaintext signaling backend; do not expose it.
- UDP: ICE host-candidate ports selected by libdatachannel.
- TCP 3389 outbound from the gateway to each allow-listed Windows target.

No STUN or TURN server is required while browser and gateway can reach one
another directly on the LAN.

## Operations

Open `https://GATEWAY-IP:18080/admin` and authenticate with the gateway access
token to view target occupancy, session duration, frame counters, traffic, and
drops. The page keeps the token only in memory. Its disconnect action closes
the selected signaling peer, which releases the matching RDP, encoder, and
input session. The remote page may authenticate again and reconnect normally.
The page also shows the 30 newest entries from a bounded 200-entry in-memory
audit trail. Events include session creation/end, RDP state changes, duplicate
connection rejection, and administrator disconnect requests. The trail is
cleared when the service restarts and never records credentials or user input.

The management API requires `Authorization: Bearer TOKEN` on every request:

- `GET /api/admin/state` returns service, target, and active-session state.
- `POST /api/admin/disconnect` accepts a JSON `peerId` and closes that peer.

Target credentials are never included in either response. An invalid or
missing token returns HTTP 401.

`GET /healthz` returning `{"status":"ok"}` proves the HTTPS listener is
alive. `peers` and `sessions` show live resource counts, while
`capturedFrames`, `encodedFrames`, `droppedFrames`, and `sentBytes` are
process-lifetime aggregate counters for the active video pipelines. For a
30 FPS session, `capturedFrames` and `encodedFrames` should each increase by
about 150 over a five-second sample. A stable `droppedFrames` value is healthy;
continuous growth indicates the encoder or network sender cannot keep up.
`rdpInputEvents` counts input messages handed to FreeRDP.

A failed RDP session retries with an interruptible exponential delay of 2, 4,
8, 16, then at most 30 seconds. A connection that remains healthy for at least
30 seconds resets this delay. This prevents clients using the same constrained
Windows account from creating a rapid reconnect storm. WebSocket clients retry
signaling without persisting the access token to disk. SIGINT and SIGTERM stop
the capture and encoding threads and release both listening ports.

Unauthenticated signaling sockets are removed after 10 seconds. Once a target
is selected, WebRTC must reach the connected state within 20 seconds or the
peer and its RDP session are released. These deadlines prevent abandoned LAN
connections from consuming session and encoder capacity.
# Multiple users

Keep the primary token in `REMOTELINK_ACCESS_TOKEN_FILE`. To authorize additional
users with independent tokens, set `REMOTELINK_ACCESS_TOKENS` to a comma-separated
list. HTTP API and WebSocket signaling use the same constant-time validation.
