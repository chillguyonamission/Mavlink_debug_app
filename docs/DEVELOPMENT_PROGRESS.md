# Development progress

Where the project stands. "Tests" means the hardware-free suite against a fake payload on loopback; "hardware" means a
real unit. Measured hardware results are in [HARDWARE_RESULTS.md](HARDWARE_RESULTS.md); changes by date are in
[CHANGELOG.md](../CHANGELOG.md).

## Feature status

| Feature | Since | Tests | Hardware |
|---|---|---|---|
| Connection, profiles, link status | 2026-10-22 | ✅ | ✅ VIO |
| MAVLink monitor, decoding, export | 2026-10-22 | ✅ | ✅ VIO |
| Network capture (libpcap) | 2026-10-22 | ✅ `.pcap` files | Live capture not yet run on native Linux |
| Gimbal modes, control source, rate | 2026-10-22 | ✅ | ✅ VIO 3.1.8.21 (OFF, RESET, MAPPING not run) |
| Move to angle (steered by rate) | 2026-10-23 | ✅ | ✅ VIO 3.1.8.21 |
| Zoom, focus, view source, camera settings | 2026-10-22 | ✅ | ✅ VIO |
| Photo, recording | 2026-10-22 | ✅ | Not run (writes to the SD card) |
| Object tracking | 2026-10-23 | ✅ | ✅ VIO 3.1.4.18; not rechecked on 3.1.8.21 |
| Command and ACK tracking | 2026-10-22 | ✅ | ✅ VIO |
| RTSP video | 2026-10-22 | ✅ | ✅ VIO over TCP; UDP not yet on native Linux |
| ZIO, MB1 (Lynx, GHadron), OrusL profiles | 2026-10-23 | ✅ | Not run |
| Object detection, data window | 2026-11-02 | ✅ | Not run |
| Stream and FOV telemetry | 2026-11-02 | ✅ | Not run |
| IR FFC, EO one-push white balance | 2026-11-02 | ✅ | Not run |
| Gimbal settings read and write | 2026-11-02 | ✅ | Not run |
| RTP/TS video + KLV | 2026-11-03 | ✅ synthetic stream | Received live once, VIO 3.1.9.24 |
| Stream bitrate | 2026-11-04 | ✅ | Not run |
| Re-request streams after a payload reboot | 2026-11-13 | ✅ | Found on a VIO; fix not rechecked |
| Windows build | 2026-11-13 | ✅ CI | Not run on a Windows PC |
| DIAGNOSE view | 2026-11-13 | ✅ loopback | Not run |
| Gamepad, fixed Xbox layout | 2026-11-20 | ✅ | ✅ VIO, Xbox Series X over USB (Linux) |
| Gamepad configuration page | 2026-11-20 | ✅ | Not run; roll, triggers, D-pad, tracking and tele / wide not run |

### Not built, on purpose

| Feature | Waiting for |
|---|---|
| Stream resolution and profile | Values, persistence and RTSP recovery unverified |
| Standby, restart, OrusL SpotAE | Recovery unverified; no OrusL unit |
| GPS and time forwarding | The app has no autopilot source |
| Detected box sent to the tracker | No source confirms it works on any model |

Details for each are in [API_INTEGRATION.md](API_INTEGRATION.md) under *Not built*.

## Acceptance checklist (first release)

| # | Criterion | Status | Evidence |
|---|---|---|---|
| 1 | Builds in the agreed environment | ✅ | Fresh clone with `scripts/build.sh` on Ubuntu 24.04: 0 warnings in app code. CI builds Ubuntu and Windows |
| 2 | IP and port editable without recompiling | ✅ | Connection bar and profiles; the SDK takes them at runtime (`T_ConnInfo`) |
| 3 | Connect/disconnect with the real SDK transport | ✅ | `payload_link` test; on a VIO, repeated connects; disconnect < 2 s with a silent peer (SDK patch) |
| 4 | Meaningful connection/protocol status | ✅ | Socket open → bytes → valid MAVLink → heartbeat → stale, from observed traffic only |
| 5 | Controls send verified commands through the SDK | ✅ VIO | Frames checked byte for byte (`payload_control` test); ACKs and telemetry on a VIO (`hw_vio_commands`) |
| 6 | Inspector shows real TX/RX from the actual path | ✅ | Observer patch on the SDK's `UDP_Port`; the SDK's own automatic traffic is visible |
| 7 | Known messages decode correctly | ✅ | Dialect CRC and field tables; tlog verified with pymavlink; VIO values match its overlay and identity |
| 8 | Unknown and invalid traffic handled transparently | ✅ | BAD_CRC / UNKNOWN_ID / TRUNCATED / GARBAGE kept with their raw bytes (`mavlink_frame` test) |
| 9 | Filter, inspect frames, export logs | ✅ | Filters, details, hex view; CSV, JSON Lines, and tlog export and recording |
| 10 | Watcher does not block the UI or control traffic | ✅ | Bounded queues; parsing on the GUI timer; files on a writer thread; video and capture on their own threads |
| 11 | Disconnected hardware and unsupported features handled safely | ✅ | Controls disabled while disconnected; profile-driven options; OFF needs confirmation |
| 12 | Automated tests for parsing, connection, logging | ✅ | 15 CTest programs, hardware-free |
| 13 | Docs cover dependencies, build, configuration, operation, testing, limitations | ✅ | README and `docs/` |

## Open hardware checks

Each needs the user's go-ahead when it changes payload state.

| Check | Command | What it answers |
|---|---|---|
| Gimbal OFF / RESET / MAPPING | Gimbal column; OFF asks for confirmation | Do the flag-route modes work on 3.1.8.21 and later? |
| Photo and recording | Camera column (writes to the SD card) | ACK and `CAMERA_CAPTURE_STATUS` follow? |
| Tracking on 3.1.8.21 and later | Select target, then click the video (the gimbal follows) | Still works after the mode changes? |
| Gamepad configuration | Analyze › Gamepad: reassign, invert, roll, triggers, D-pad, START tracking, tele / wide | Each new function on the payload |
| Windows | Portable zip on a Windows PC with a VIO | Link, video, gamepad (XInput), KLV interface names |
| RTSP over UDP, live capture | Native Ubuntu | Both were impossible under WSL2 |
| Detection on a VIO | `hw_vio_commands --ip <IP> --steps detection` (TRACK_MODE changes, then is restored; nothing moves) | Do packets arrive? Do real boxes pass validation? Does rate 0 stop them? |
| Forwarding | `hw_vio_commands --ip <IP> --steps detection-forward` (**the gimbal follows**, then returns) | Does a detected box work as a tracker target after TRACK_MODE returns to tracking? |
| Overlay | Operate, with video and an object in view | Do boxes sit on the objects, at the stream's real resolution? |
| Each non-VIO model | Connect to a real unit, read-only: Analyze › Telemetry, then Camera settings › Read (nothing moves, nothing is written) | What model name and compids does it report? Which settings does it list? Which requests does it answer? |
| MB1 settings | One harmless setting, with and without `SETTING_TARGET` (changes payload state) | Does `SETTING_TARGET` redirect other settings? Is `DETECTION_EN` needed for detection? Does FFC work? |
| Data window | Detection data…, while detection runs | Do real frame IDs increase by one? Do the COCO guesses match what the camera sees? |
| Stream and FOV | Open Analyze › Telemetry (read-only) | Does the payload answer? Which `camera_device_id` does each FOV reply carry? |
| FFC and white balance | Buttons in Operate › IMAGE CALIBRATION, one at a time (changes camera state) | Does each get an ACK? Does `FFC_MODE` appear and follow the mode? Does one-push need White balance = One push? |
| Gimbal settings, read | Analyze › Gimbal settings › Read from gimbal (read-only) | Does the gimbal answer? How many replies are lost? Which types does it report? |
| Gimbal settings, write | One harmless parameter, restored afterwards (a stored gimbal setting) | Does the read-back follow? Does it survive a restart? |

### KLV and RTP/TS

Built on 2026-11-03 against a synthetic stream. On 2026-11-04 a VIO on 3.1.9.24 sent video and KLV over UDP unicast to
`192.168.55.100:8554` and the app played both; the questions below are still open.

| Check | How | What it answers |
|---|---|---|
| Stream topology | ViO over USB-C, RTP/TS source with defaults, read-only | Does this firmware send `239.255.0.1:8554`, and is that the same on release firmware? The only evidence is one beta capture |
| RTSP and RTP/TS together on the payload | Passive listen while the RTSP source plays | Does it send both at once? This decides whether a metadata-only KLV mode is worth building |
| Tag set | Analyze › KLV, with and without GPS | Do position and platform tags appear once the payload has GPS? |
| Tag 2 | Duplicates counter | Does tag 2 advance on every packet on real firmware? If not, metadata would read as stale |
| Soak on target | `test_klv_stream soak` covers loopback only | Memory, descriptors and threads on the target machine with the real 720p stream |
| `openh264dec` on the real stream | Plain `gst-launch`, read-only | Does it decode the payload's H.264? It would avoid the `avdec_h264` memory growth |

## Known limitations

- **Models:** only VIO has run on hardware. ZIO, MB1 and OrusL come from their SDK headers; their IDs, values, tracking
  and control source need a real unit.
- **Angle:** VIO 3.1.8.21 accepts no angle command, so Move to angle steers by rate. Zoom to an absolute level is not
  exposed, since its range is not documented per model.
- **KLV:** not synchronised to video frames; no platform position or target coordinates; cannot run alongside RTSP
  video; bare KLV over UDP is not supported. `avdec_h264` grows by about 20 MB per hour (see [TESTING.md](TESTING.md)).
- **Video:** no recording or snapshot from the app. Under WSL2 NAT only TCP works.
- **Recordings:** `.pcap` files load into the monitor; the app's own `.tlog` / `.jsonl` do not.
- **WSL2:** if replies do not arrive, set `networkingMode=mirrored` in `%UserProfile%\.wslconfig`, then `wsl --shutdown`.
- **SDK:** `checkPayloadConnection()` blocks forever, so the app judges the link from observed traffic.
- **Licence:** the PayloadSDK is GPL-3.0; distributing this app requires releasing it under GPL-3.0.

## History notes

### Audit of 2026-11-02

| Change | Why | Files | Tested |
|---|---|---|---|
| Focus disabled on MB1 | The SDK says MB1 has no focus controls, but the buttons were live | `payload_profile*`, `payload_pages.cpp` | `main_window` |
| A Lynx or GHadron on MB1 no longer warns of a wrong model | The check compared names exactly, so it would have warned on a correct setup. "ORUS-L" on OrusL too | `payload_profile.h`, `main_window.cpp` | `payload_control`, `main_window` |
| EO-only models get no IR, rangefinder or view requests | On connect, ZIO was asked for streams it has no hardware for, and for IR FOV every 2 s while Telemetry was open | `payload_link.*`, `main_window.cpp` | `payload_control` |
| The PAYLOAD lamp accepts any camera or gimbal ID | The SDK and the rest of the app do. A model not on 100 and 154 would have shown 2/3 forever | `link_health.*`, `main_window.cpp` | `link_logic` |
| FFC mode and IR temperature streams requested on connect | `ui_demo` does, and the app's list claims to follow it. Read-only | `payload_link.cpp` | `payload_control` |
| `STORAGE_TYPE` added to MB1 settings | `ui_demo` uses it. Disabled unless the camera reports it | `payload_profile_mb1.cpp` | `payload_control` |
| Hidden payload pages stop refreshing | Telemetry and others decoded and redrew at 4 Hz out of sight | `payload_pages.*` | Full suite |
| The capture drop count resets on connect | It warned forever after one overflow | `payload_link.cpp` | Not tested: a one-line reset |
| Test fixes | A fixture overran string literals; `qApp` was misused in a test without a GUI | `tests/` | Memory checks |
| New tests | SDK model inventory, reconnect and video restart resources | `tests/` | Full suite |

Checked and deliberately not changed:

- **Performance:** measured, and nothing is a bottleneck (`docs/ARCHITECTURE.md`).
- **Dead code:** the linker's unused-section list has no unused app function.
- **Duplicate palette lists:** kept, since each list is tied to its own model header at compile time.
- **Frame copy in `VideoPlayer::onSample()`:** avoidable, but holding decoder buffers risks stalling the pipeline, for about one 3.7 MB copy per frame. Not measured.
- **SDK races:** see `docs/ARCHITECTURE.md`.

### Test log

| Date | Point | Result |
|---|---|---|
| 2026-11-20 | Gamepad configuration page, `build/`; CI Linux and Windows | 15/15 passed; CI green |
| 2026-11-13 | Windows port; CI | Ubuntu 13/13, Windows (MSVC) 12/12 |

Run on 2026-11-02, native Ubuntu, build directory outside the repository (see `docs/TESTING.md`).

| Point | Result |
|---|---|
| Before any change | 9/9 passed |
| After object detection | 10/10 passed, three runs in a row |
| After phase 1 | 10/10 passed |
| After phase 2 | 10/10 passed |
| After phase 3 and final docs | 10/10 passed |
| After the detection data window, in `build/` | 10/10 passed |
| After the audit, in `build/` | 10/10 passed, three runs in a row |
| After the audit, AddressSanitizer and UBSan | 10/10 passed, no reports. App and test code only: leaks inside Qt, GLib and GStreamer are suppressed |
| After the audit, ThreadSanitizer | 4 tests: all test cases passed, and each binary exits 66 because reports exist. None on the app's own data (`docs/ARCHITECTURE.md`) |

`hw_vio_commands` was built and never run. No payload was connected or commanded during this work.

Run on 2026-11-03, native Ubuntu, for the KLV integration. No payload was reachable, and nothing was commanded.

| Point | Result |
|---|---|
| Before any change, in `build/` | 10/10 passed |
| After the integration, in `build/` | 13/13 passed |
| `klv_stream` | 25 test functions passed, three runs in a row. `soak` and `screenshots` skip unless asked for |
| AddressSanitizer and UBSan on `klv_stream`, `metadata_model`, `misb0601` | Passed, no reports. Leak check on `klv_stream` only, with leaks inside Qt, GLib and GStreamer suppressed: none |
| ThreadSanitizer on `klv_stream` | All test functions passed, and the binary exits 66 because reports exist. None has both accesses in app source. GStreamer and GLib are not instrumented, so those reports were not individually proven false |
| 50 start/stop cycles | No thread or non-GPU descriptor growth |
| 60-minute soak, synthetic 320×240 stream over loopback | Decoding throughout, 0 KLV drops, 0 malformed, descriptors and threads flat. **Memory not stable:** 104 MB to 124 MB, about 20 MB/h, from `avdec_h264`. See *Known resource issues* in `docs/TESTING.md` |
