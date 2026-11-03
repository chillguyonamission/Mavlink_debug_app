# Project notes for Claude Code

Gremsy MAVLink debug app: a Qt 6 / C++17 desktop tool that controls Gremsy payloads through the
official PayloadSDK and inspects the real MAVLink traffic. Read `README.md` first (design, hardware
results, acceptance checklist, limitations). For setup on native Ubuntu, see
`docs/INSTALL_UBUNTU.md`.

## Status (2026-10-22)

- Stages 1–6 are done: connection and profiles, the inspector with network capture,
  VIO gimbal and camera control, telemetry, command/ACK tracking, video, clean-build validation, and
  install.
- Development so far was on Windows + WSL2 (Ubuntu 24.04). Work is moving to the user's native
  dual-boot Ubuntu 24.04 to finish the hardware testing. The native build passes 10/10.
- UI redesign (2026-10-22) is done:
  - Dark station theme (`src/theme.*`).
  - One Operate workspace (gimbal | video with HUD | camera), plus an Analyze view.
  - A collapsible MAVLink monitor and a SETTINGS drawer.
  - Object tracking control added.
- 2026-10-23 (native Ubuntu, VIO over USB-C):
  - Tracking fixed and verified (see facts).
  - Gimbal speed slider with the VIO rate scale.
  - Camera settings dialog (30 settings).
  - Boot splash.
  - Fixed a crash on exit while the video plays, and HEARTBEAT being matched as a command's answer.
  - `hw_vio_commands --steps camera,angle,rate,mode-param,tracking,settings`: 59/59 checks passed (on 3.1.4.18).
- 2026-10-23, later: the VIO was updated to software **3.1.8.21**:
  - Modes now use the flag route.
  - The gimbal panel shows and switches the control source (`GB_FW_FLAG`).
  - Rate compensation removed.
  - The automatic info request now waits for the camera's heartbeat.
  - `--steps control-onboard,camera,rate,mode-flags` passed.

- 2026-11-02: object detection (overlay, states, selection), stream and FOV telemetry, IR FFC and one-push white balance,
  and gimbal settings. **All tested only against the fake payload.** The checks still needed are in
  `docs/DEVELOPMENT_PROGRESS.md`. See `docs/SDK_FEATURE_MATRIX.md` and `docs/API_INTEGRATION.md`.

- 2026-11-02, later: audited the payload profiles against the SDK. Four targets, four profiles, none missing; Lynx and GHadron
  use MB1. Coverage matrix in `docs/SDK_FEATURE_MATRIX.md`. **ZIO, MB1 and OrusL still never ran on hardware.**

- 2026-11-03: RTP/TS video + KLV integrated from `Gremsy_KLV_streamer` (`docs/KLV_STREAM.md`). **Tested only against a synthetic stream;**
  no payload was reachable. Open hardware checks are in `docs/DEVELOPMENT_PROGRESS.md`.

## Next steps

1. Native build: `scripts/build.sh` should report 13/13 tests passing. If it fails with a `CMakeCache.txt` path error, the
   repository moved: run `cmake --fresh -S . -B build`. Build into `build/` so the user's binary has the work.
2. On native Linux, verify RTSP over UDP and live network capture (both impossible under WSL2).
   `ffmpeg -rtsp_transport udp` got only 3 frames on 2026-10-23 (TCP fine). The laptop then had two
   addresses in 192.168.55.0/24 (`.3` and `.100`), which may matter.
3. Ask the user for the original `C_V_RGAIN`, `C_V_BGAIN` and `C_V_ICR_THR`. They were set to 0 by
   mistake on 2026-10-23 (originals were 128–255, unknown). They have no effect while white balance is
   Auto and ICR is Manual.
4. ZIO / MB1 / OrusL profiles exist, but only from their headers. The user accepted them untested
   (2026-10-23). Check them when a unit is available.

## Rules the user set

- **Ask first** before any command that moves the gimbal or changes the payload's state.
  - Approved so far: camera zoom/focus/settings, small gimbal angle/rate moves, and LOCK/FOLLOW mode
    changes, each undone afterwards.
  - Approved on 2026-10-23: object tracking (the gimbal follows), and writing camera settings, each
    restored afterwards.
  - **Not approved:** photo capture or recording (SD writes), gimbal OFF / RESET / MAPPING, and
    changing `RC_MODE` (a persistent setting). Re-ask for these.
  - **Not yet approved (new, 2026-11-02):** `TRACK_MODE` = detection, `hw_vio_commands --steps detection` or
    `detection-forward`, FFC and one-push commands, and gimbal parameter writes.
  - Never write a camera setting value that was not read back through a decoder already checked
    against the camera (`--steps settings-read`).
- Commit and push to GitHub when a stage or fix is finished.
  - Author: `Huynh Gia Bao <baohuynh2905@gmail.com>`. Set it per clone with `git config user.name` /
    `git config user.email`; it is not stored in the repo.
  - Never use support@gremsy.com for commits.
- Keep changes small, match the existing code style, add a test for new logic, and do not claim a
  test passed without running it.

## Facts that are easy to get wrong

- **Transport:** the SDK's `UDP_Port` is a UDP client (ephemeral local port).
  `patches/payloadsdk-transport-observer.patch` (applied by CMake) adds the TX/RX observer and fixes
  the `sdkQuit()` hang. Never edit the submodule directly; change the patch.
- **Gimbal modes:** on 3.1.8.21 only the flags in `GIMBAL_DEVICE_SET_ATTITUDE` work (Gremsy's way since
  v307.54); `PARAM_EXT_SET GB_MODE` is rejected. On 3.1.4.18 it was the other way round.
- **Gimbal control source `GB_FW_FLAG`** (3.1.8.21):
  - 0 = Autopilot (the user's setting): the payload drops all our `GIMBAL_DEVICE_SET_ATTITUDE`.
  - 1 = Onboard computer: modes and rates work.
  - It is a stored setting. The user chose to switch it from the gimbal panel themselves; tests restore it.
- **Gimbal rate:** in real units on 3.1.8.21 (10°/s gives about 12°/s peak). 3.1.4.18 turned at
  × π/18; that compensation is gone.
- **Gimbal angle:** works on 3.1.4.18 (yaw lands about 2.7° off). On 3.1.8.21 no angle command works, so
  `gimbalAngle()` steers by rate from the measured attitude (gain 1/s, 0.5° tolerance); this was verified.
  If Gremsy names a real angle command, swap it in.
- **Attitude stream (3.1.8.21):** bursts at 10 Hz with pauses of 0.6–0.8 s every second. Staleness limits
  must allow 1.5 s.
- **Auto info request:** waits 300 ms after the first camera frame. The SDK's own message thread updates the
  camera compid only after the transport observer has already seen the frame.
- **Gimbal flags on every command:** each `GIMBAL_DEVICE_SET_ATTITUDE` (rate, angle) carries mode flags, and
  3.1.8.21 applies them. The SDK never initialised its copy (`current_gb_device_flags`) and refreshes it only
  with a `regPayloadParamChanged()` callback. The app patch adds `setGimbalDeviceFlags()`;
  `PayloadLink::setMotionFlags()` sends the reported mode, or for 1 s the mode just commanded.
- **Camera settings list:** the reply is a burst, and about a third of it can be lost on the USB link. Pages call
  `readMissingCameraSetting()`, which re-asks by ID every 2 s.
- **SDK camera compid:** the SDK addresses the camera at 101 until it hears compid 100, so the automatic
  requests wait for the camera's heartbeat.
- **Tracking:**
  - The gimbal follows only if the last gimbal input was not an angle command, so
    `trackingTarget()` sends rate 0 first.
  - `TRK_POS_X/Y` is the box's top-left corner.
  - The tracker space is 1920×1080; this was checked against the OSD's own box.
- **Camera settings:**
  - The `PARAM_EXT_ACK` carries no value.
  - The camera lists `C_V_GAIN` and `C_V_BrP` and rejects the header's `_HS`/`_LS` IDs.
  - It answered parameter messages only from source component IDs it had already seen (new compids
    195/196 got no reply, 191/193/194 did). The app's SDK compid works.
- **Detection packets:** MAVLink 2 trims trailing zeros, so a short `V2_EXTENSION` is normal and a length check would drop
  good data. Geometry validation rejects what the wire did not carry. `TRACK_MODE` selects tracking or detection, so
  the two are treated as exclusive. Forwarding a box to the tracker is held back until `--steps detection-forward` answers it.
- **Models:** the SDK has four build targets (`VIO`, `ZIO`, `MB1`, `ORUSL`). MB1 is a mainboard: Lynx and GHadron build with it and
  have no IDs of their own. The SDK has no plain Orus target. Only VIO's reported model name ("Vio") has been seen on a real unit.
- **Video descriptors:** GStreamer's `vah264dec` keeps one `dmabuf` per pipeline, without any app code. It is not an app leak.
- **SDK traps:** `setPayloadGimbalParamByID()` `strcpy`s into a `char[16]`, so 16-character IDs are refused.
  `getPayloadCameraFFCMode()` is empty. `getPayloadStreamBitrate()` always returns 0.
- **Camera component:** compid 100 (not 101).
- **REQUEST_MESSAGE:** answered with the message itself but no COMMAND_ACK.
- **Sequence numbers:** the VIO payload core and camera reorder them (N+1 before N). This is not
  loss.
- **Video:** `rtsp://<IP>:8554/payload`, H.264 1280×720 at about 30 fps. Under WSL2 NAT only TCP
  works.
- **KLV is unverified on hardware from this app.** The only topology evidence is one capture from ViO beta firmware (2026-10-29): video and
  KLV in one TS on `239.255.0.1:8554`, with tags 1, 2, 16–21 and 65 only. Those are not universal settings.
- **`avdec_h264` grows by about 20 MB/h** with no app code (`docs/TESTING.md`). A flat line needs proof that frames flowed: `openh264dec` stayed flat
  and did decode, but it exited at once on an x264 stream.
- **KLV and video are not synchronised.** The panel shows the latest stream metadata. Never present it as describing a frame.
- **Build:** a full parallel build can exhaust memory. Use `JOBS=2 scripts/build.sh` if the machine
  freezes or a compile is killed.
