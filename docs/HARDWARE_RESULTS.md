# Hardware results

Everything here was measured on a real **VIO** connected over USB-C (RNDIS; the payload at `192.168.55.1`, the laptop at
`192.168.55.100`). No other model has run on hardware yet. Checks still open are in
[DEVELOPMENT_PROGRESS.md](DEVELOPMENT_PROGRESS.md).

## Tools

| Tool | What it does | Moves the payload |
|---|---|---|
| `build/mavlink_capture --ip <IP> --seconds 30 --out capture` | Connects through the GUI's code, records `.tlog` and `.jsonl`, prints components, message rates, sequence statistics and request results | No: sends only what the GUI sends on connect |
| `build/hw_vio_commands --ip <IP> --steps <list>` | Control-path checks with pass/fail per step; each change is undone | **Yes**, per step. Never run by `ctest` |
| `HW_IP=<IP> SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/test_main_window hardwareScreenshots` | Read-only GUI screenshots of Operate and each Analyze tab | No |

## The link (2026-10-22, software 3.1.4.18, hardware V42A)

- **Quality:** over 20–30 s captures, 0 CRC errors, 0 junk bytes, 0 capture drops. WSL2 NAT worked for this link.
- **Components:**

  | Component | Messages |
  |---|---|
  | Payload core 1/26 | HEARTBEAT about 2 Hz; DEBUG parameters about 40 Hz once requested |
  | Camera 1/100 (not 101, as Gremsy's command list says) | HEARTBEAT, STORAGE_INFORMATION, CAMERA_CAPTURE_STATUS about 1 Hz |
  | Gimbal 1/154 | GIMBAL_DEVICE_ATTITUDE_STATUS, MOUNT_ORIENTATION, RAW_IMU about 10 Hz; HEARTBEAT, MOUNT_STATUS 1 Hz |

- **Acknowledgements:** `MAV_CMD_REQUEST_CAMERA_*` and `REQUEST_STORAGE_INFORMATION` in 3–250 ms,
  `SET_MESSAGE_INTERVAL` in 1–3 ms. `MAV_CMD_REQUEST_MESSAGE` gets the message within about 4 ms but never a COMMAND_ACK;
  the app shows "Answered (message, no ACK)".
- **Sequence numbers:** the payload core and camera sometimes send N+1 before N (two threads share one counter). Nothing
  is lost; the statistics count it as *reordered*.
- **Video:** H.264 1280×720, about 31 fps over RTSP/TCP while MAVLink ran at about 90 frames/s, no errors on either.

## Control on software 3.1.4.18 (2026-10-23)

`hw_vio_commands --steps camera,angle,rate,mode-param,tracking,settings`: **59/59 checks passed**. Small motions, each undone;
no photo or recording.

| Area | Result |
|---|---|
| Camera | `C_SOURCE`, continuous zoom and auto focus acknowledged in 1–4 ms. `EO_ZOOM` went 1 → 2.16 → 1 |
| Gimbal angle | Pitch −10° and back in 100–300 ms. Yaw lands about 2.7° off |
| Gimbal rate | The VIO turned at rate × π/18; sending × 18/π gave 9.8–9.97°/s for 10°/s |
| Modes | `PARAM_EXT_SET GB_MODE` FOLLOW / LOCK acknowledged in 1–2 ms; flags `0x3C` → `0x2C` → `0x3C`. The flag route did nothing |
| Tracking | A centre pick was TRACKED within 0.1 s. An off-centre pick turned the gimbal 20.7° and centred the target within 55 px in about 3.5 s. Without a rate-0 command first, the gimbal did not move |
| Camera settings | All 30 written with their current values and acknowledged; three changed, read back and restored |
| Not run | OFF, RESET, MAPPING, photo, recording |

## Control on software 3.1.8.21 (2026-10-23, after the update)

`hw_vio_commands --steps control-onboard,camera,rate,mode-flags`: all checks passed.

- **Control source `GB_FW_FLAG`:** with Autopilot (the payload's setting) every `GIMBAL_DEVICE_SET_ATTITUDE` is ignored
  and `MAV_CMD_DO_MOUNT_CONTROL` is denied. With Onboard computer, modes and rates work.
- **Modes:** only the flags in `GIMBAL_DEVICE_SET_ATTITUDE` work now; `GB_MODE` followed FOLLOW → LOCK → FOLLOW (2 → 1 → 2).
  `PARAM_EXT_SET GB_MODE` is rejected.
- **Rate:** real units now (10°/s gave a 12.2°/s peak), so the × 18/π compensation was removed.
- **Mode kept while moving:** every rate message carries mode flags, which this version applies. The SDK sent
  uninitialised flags, so the arrows switched LOCK to FOLLOW; the app now sends the reported mode.
- **Angle:** no angle command works. **Move to angle is steered by rate** from the measured attitude (gain 1/s, stops
  within 0.5°). 10° pitch and 20° yaw moves took about 2.5 s and ended 0.1–0.5° off.
- **Attitude stream:** bursts at 10 Hz with pauses of 0.6–0.8 s every second.

## Software 3.1.9.24 (2026-11-04 to 2026-11-20)

- **Live app session:** heartbeat, RTP/TS video and KLV worked. That unit sends KLV over UDP unicast to
  `192.168.55.100:8554` and has RTSP off, so the RTP/TS address must be that, not the default multicast group.
- **New PayloadSDK `2021d9f`:** a read-only capture gave no errors and the same answers as before.
- **Device info (2026-11-20 probe):** CAMERA_INFORMATION "Gremsy" / "Vio Payload G1" / 3.1.9.24;
  GIMBAL_DEVICE_INFORMATION "GREMSY" / "VIO", hardware version 0. COMPONENT_INFORMATION_BASIC is not answered. The
  sensor name and hardware revision shown in the payload's own logs are not available over MAVLink.
- **Gamepad:** the user ran the fixed Xbox layout with a wired Xbox Series X pad; every function worked.

## Firmware and SDK quirks

- `CAMERA_INFORMATION.firmware_version` is byte-reversed (`0x03010412` for 3.1.4.18). The Telemetry tab shows both
  readings.
- `vendor_name` and `model_name` carry leftover text after the NUL (for example `/dev/ttyTHS1`); the app stops at the NUL.
- No `DISTANCE_SENSOR`: the rangefinder comes as the `LRF_RANGE` parameter (2–4 on a desk; unit undocumented).
- The camera answers parameter messages only from source component IDs it has already seen.
- On connect the SDK sends a burst of stream-interval requests, one every 20 ms, that the user did not trigger.
- The app's TX frames carry several independent sequence counters (MAVLink keeps one per C++ file), so TX gaps are not
  loss.
- The SDK writes at most one queued message every 20 ms, so a command shows in TX slightly after the call.
