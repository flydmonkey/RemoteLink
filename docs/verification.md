# Verification checklist

The LAN release is verified with the following gates:

1. `cmake --build build --parallel` succeeds.
2. `ctest --test-dir build --output-on-failure` passes core queue, pacing,
   and exact host allow-list tests.
3. HTTPS `/healthz` reports `status=ok` and `tls=true`; plain HTTP is rejected.
4. `tests/signaling_smoke.py` proves an invalid WSS token is rejected and a
   valid token is accepted.
5. `remotelink_webrtc_smoke` authenticates, negotiates WebRTC, receives at
   least three H.264 RTP packets, opens the input DataChannel, and sends pointer
   movement, left-button down/up, wheel, and Shift down/up events.
6. The `rdpInputEvents` health counter increases by six during the native smoke
   test, proving those events reached the FreeRDP input API.
7. A trusted browser loads the HTTPS page, reports `WebRTC: connected`, shows a
   growing packet/decoded-frame count, and displays the real Windows desktop.
8. With an active browser peer, SIGTERM exits the process and releases the public
   listener; after restart the unchanged page reconnects and resumes decoding.
9. Exactly one `remotelink` process exposes TCP 18080; its internal signaling
   backend on TCP 18081 is bound only to `127.0.0.1`.
10. Two simultaneous clients report `sessions=2`; closing either client returns
    the count to one without interrupting the other stream.
11. An unauthenticated WSS client increases `peers` temporarily and is removed
    after the 10-second authentication deadline. A WebRTC negotiation that
    does not connect within 20 seconds is removed with its RDP session.
12. The browser reports `Windows 已连接` after FreeRDP succeeds. Selecting
    “切换主机” returns to the target list and reduces `sessions` to zero without
    disconnecting the authenticated signaling peer.
13. The text-paste dialog sends bounded UTF-8 input as RDP Unicode key down/up
    events. Window blur, page hiding, and pointer cancellation release tracked
    keys and mouse buttons so modifiers cannot remain stuck remotely.
14. Browser decoder metrics sampled over five seconds sustain at least 27 FPS
    at 1280x720/30 without a growing latency backlog.
15. With one active session, two `/healthz` samples five seconds apart show
    `capturedFrames` and `encodedFrames` increasing by approximately 150,
    `sentBytes` increasing, and no continuously growing `droppedFrames` count.
16. While one target is active, a second peer selecting the same host and
    Windows username receives `target-user-busy` and does not create another
    RDP connection. A different host or username is still allowed.
17. `/admin` rejects a wrong token, refreshes target/session counters every two
    seconds, and never displays target passwords. Its disconnect action removes
    the selected session; an open remote page can then reconnect cleanly.
18. The unified activity view identifies RDP, VNC, and SSH sessions by
    RemoteLink user, target, address, timestamp, and disconnect reason.
19. RDP, VNC, and SSH management APIs expose only `has*` credential status;
    clearing a credential removes its protected secret file.
20. A new or reset SSH target rejects user sessions until an administrator
    tests and explicitly confirms the observed SHA-256 host fingerprint.
21. `npm run test:e2e` starts the three protocol containers and runs the RDP
    reachability/credential, VNC proxy, and SSH trust/terminal integrations.

Do not put access tokens or RDP passwords into this file, command history,
screenshots, source files, or test fixtures.
