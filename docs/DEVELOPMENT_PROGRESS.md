# Development progress

Software tests and hardware validation are tracked separately. A passing test here means a fake payload on
loopback, never a real unit.

## Status by phase

| Phase | State | Software tests | Hardware |
|---|---|---|---|
| Object detection and bounding boxes | Implemented; forwarding to the tracker held back | `detection`, `main_window` | **Not run** |
| Detection data window | Implemented | `detection`, `main_window` | **Not run** |
| 1. Stream and FOV telemetry | Implemented | `payload_control`, `main_window` | **Not run** |
| 2. IR FFC, EO one-push white balance | Implemented | `payload_control`, `main_window` | **Not run** |
| 3. Gimbal settings read and write | Implemented | `payload_control`, `main_window` | **Not run** |
| 4. Stream bitrate, resolution, profile | **Not built**: values, persistence and RTSP recovery unverified | None | Not run |
| 5. Standby, restart, OrusL SpotAE | **Not built**: recovery unverified, no OrusL unit | None | Not run |
| 6. GPS and time forwarding | **Not built**: the app has no autopilot source | None | Not run |

| Payload profile audit | Done: four SDK targets, four profiles, none missing. See `docs/SDK_FEATURE_MATRIX.md` | `payload_control`, `main_window` | **Not run** on ZIO, MB1 or OrusL |

What each unbuilt phase is waiting for is in `docs/API_INTEGRATION.md` under *Not built*.

## Audit of 2026-11-02

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

## Hardware validation still needed

Each of these needs your go-ahead, because it changes payload state.

| Check | Command | What it answers |
|---|---|---|
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

## Test log

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
