# Architecture

## LAN MVP

```text
Browser
  |-- HTTPS UI/API and WebSocket signaling at /ws (TCP 18080)
  |-- WebRTC H.264 video
  `-- WebRTC data channel (keyboard, pointer, clipboard)
                         |
                         v
                 remote-gateway
  HTTP + /ws proxy -> loopback signaling -> peer/session manager
                         |
                         +-> peer A: FreeRDP -> frame queue -> H.264 -> WebRTC A
                         `-> peer B: FreeRDP -> frame queue -> H.264 -> WebRTC B
```

The LAN version uses ICE host candidates and does not require STUN or TURN.
Public-internet connectivity is deliberately out of scope for the MVP.

## Latency policy

Interactive control values freshness over completeness. Each session has a
small bounded frame queue. If capture outpaces encoding or network delivery,
the oldest unencoded frame is discarded. The service must never allow a video
backlog to grow without bound.

Capture uses an absolute 33.33 ms deadline rather than repeatedly scheduling
33 ms from the current time. This avoids wait-loop quantization and cumulative
drift. OpenH264 frame skipping is disabled so a healthy LAN session can sustain
the configured 30 FPS instead of silently reducing temporal smoothness. Each
encoder uses up to four fixed slices/worker threads so 1280x720 dynamic desktop
content does not bottleneck on one software-encoding thread.

Frames are complete snapshots. The browser receives encoded video frames,
instead of independently decoded PNG/JPEG rectangles, so a frame can be
presented atomically and the tearing mode seen with incremental drawing is
avoided.

## Isolation

Each authenticated signaling peer owns at most one RDP/capture/encoder/input
session. A `stop` message destroys only that session while keeping the
authenticated signaling connection available for another target selection.
Closing signaling also removes only the matching session.

The session manager reserves a case-insensitive `(host, username)` identity
before starting any worker threads. A second peer requesting the same identity
is rejected before RDP connection setup, while different hosts or Windows
usernames remain independent. Releasing or failing session construction also
releases the reservation.

FreeRDP emits `connecting`, `connected`, `retrying`, and `stopped` state updates
through the owning peer's signaling socket. These updates contain no target
credentials or server diagnostics; they let the browser distinguish an RDP
failure from a WebRTC/media failure.

The product is one deployable service, but this does not require every RDP
session to share one media or input path. A later hardening milestone may run
FreeRDP sessions in supervised child processes while preserving the same
single executable and external API.

## Security boundaries

- The browser cannot select arbitrary RDP addresses.
- RDP targets come from an administrator-controlled allow-list.
- Credentials never reach browser JavaScript and must not be logged.
- Only HTTPS is exposed; RDP and internal control channels remain private.
- The gateway access token is validated before the target list is disclosed or
  WebRTC negotiation begins.
