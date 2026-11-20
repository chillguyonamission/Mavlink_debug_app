# Changelog

Newest first, grouped by date. "Tested" means the hardware-free suite against a fake payload; "hardware" means a real
unit. Current status of each feature: [docs/DEVELOPMENT_PROGRESS.md](docs/DEVELOPMENT_PROGRESS.md).

## 2026-11-20: Gamepad

### Added

- **Gamepad control** for the gimbal and camera, written for the Xbox Series X pad. Linux reads evdev, Windows XInput;
  no new dependency. **Hardware:** the fixed Xbox layout ran on a VIO (Linux, wired pad) with every function working.
- **Analyze › Gamepad**, the configuration page from Gremsy's "Configuration Page - Gamepad control" document:
  - any function on any of 19 channels, one function per channel;
  - INVERT and DEADZONE (0–30 %, default 8 %) per stick axis;
  - live sticks and triggers, and a pad picture with each control's function;
  - saved between runs.
  - Functions: gimbal pitch / yaw / roll rate, zoom, home, LOCK/FOLLOW, photo, record, view and record source,
    tracking on/off, zoom tele / wide.
  - Tested only. Roll, triggers, D-pad, tracking and tele / wide have not run on a payload. Bluetooth is untested.

### Changed

- Checkboxes have a visible style across the app.
- The GUI test ignores a pad plugged into the PC (`MAVLINK_DEBUG_NO_GAMEPAD`).
- Documentation reorganised: a short README, plus a user guide, hardware results and one progress page in `docs/`.

## 2026-11-13: Windows and DIAGNOSE

### Added

- **DIAGNOSE view:**
  - **Health check:** link, each payload part's heartbeat and state, decoded gimbal failure flags, model, payload value
    stream, frame and send errors, sequence gaps, failed commands.
  - **Network check (read-only):** PC adapters on the payload's subnet, its TCP services, and its software list from the
    web app.
  - **SDK update check** against GitHub, and a text report.
  - Tested with a loopback network check only.
- **Web settings tab:** the payload's web app inside the app (Qt WebEngine), or **Open in browser**. The pages were
  checked in Qt WebEngine on a VIO (Setting App 2.7.0.12); the tab itself has not run against a payload.
- **Windows build:**
  - `patches/payloadsdk-windows.patch` ports the SDK: Winsock, `std::thread` / `std::mutex`, 1 ms timer resolution.
  - CI on Ubuntu 24.04 and Windows Server 2022 (MSVC).
  - Network capture on Windows needs Npcap installed by the user. UART control is Linux-only.
  - Not run on a Windows PC with a payload.
- **Portable packages from CI:** a Windows zip (Qt, GStreamer, MAVLink XML inside) and a Linux AppImage.

### Changed

- **PayloadSDK** `fba3c70` → `2021d9f` (23 upstream commits). The app's patch applies unchanged. Hardware: a read-only
  capture on a VIO (3.1.9.24) gave the same answers.
- **Gimbal control source** comes from each model's SDK header; ZIO and OrusL gain the switch. Only VIO has run on
  hardware.

### Fixed

- After a payload reboot, zoom, FFC, IR temperatures and the rangefinder stopped updating: the payload forgets the
  requested streams. The app now asks again when the payload core's heartbeat returns. Found on a VIO; fix tested only.

## 2026-11-04: Bitrate and camera settings panel

- **Added:** EO and IR stream bitrate presets (0.512–16 Mbit/s) in SETTINGS. Tested only.
- **Changed:** camera settings open as a panel under the video, replacing the MAVLink monitor until closed, so the
  picture stays visible.
- **Fixed:** the mouse wheel over a combo box no longer changes it. Scrolling past one could send a command, such as the
  gimbal control source.
- **Hardware:** the app ran live on a VIO with 3.1.9.24. Heartbeat, RTP/TS video and KLV worked over UDP unicast to
  `192.168.55.100:8554`.

## 2026-11-03: RTP/TS video and KLV

- **Added:** a second video source, H.264 from an RTP/MPEG-TS stream with MISB ST 0601 KLV from the same stream.
  - A compact KLV panel under the video, and Analyze › KLV.
  - Decoder and model come from the KLV monitor project.
  - Metadata is the latest in the stream, not synchronised to frames.
  - `avdec_h264` grows by about 20 MB per hour in this mode.
  - Tested with a synthetic stream. See [docs/KLV_STREAM.md](docs/KLV_STREAM.md).
- **Not added, on purpose:**
  - KLV alongside RTSP video.
  - A map view.
  - Frame-level KLV sync.
  - The KLV monitor's QML screens, raw recording and PMT parsing.

## 2026-11-02: Detection and newer SDK features

### Added

- **Object detection:** enable and disable, seven states, boxes on the video, click to select a box. Plus a **Detection
  data** window with a live box table, summary, Pause and a 1000-packet log.
- **Stream and FOV telemetry** in Analyze › Telemetry, shown only while a reply is fresh.
- **Image calibration:** IR FFC auto / manual / trigger and EO one-push white balance, on models that support them.
- **Gimbal settings:** a new Analyze tab that reads the gimbal's parameters and writes one after a confirmation.
- `hw_vio_commands --steps detection,detection-forward`: built, not run.
- All tested only.

### Fixed (profile audit against the SDK)

- MB1 focus buttons disabled, since the SDK has no MB1 focus control.
- Lynx and GHadron on MB1, and spellings such as "ORUS-L", no longer raise a wrong-model warning.
- EO-only models (ZIO) are no longer asked for IR, rangefinder and view streams.
- The PAYLOAD lamp counts any MAVLink camera or gimbal ID.
- The capture drop count resets on connect.

### Changed

- FFC mode and IR temperature streams are requested on connect on IR models, as the SDK's `ui_demo` does.
- `STORAGE_TYPE` added to the MB1 camera settings.
- Payload pages skip their refresh while hidden.

## 2026-10-23: VIO software 3.1.8.21 and more models

- **Added:**
  - Gimbal speed slider.
  - Camera settings dialog (30 VIO settings).
  - Boot splash.
  - ZIO, MB1 and OrusL profiles from their SDK headers.
- **Fixed:**
  - Tracking: send rate 0 first so the gimbal follows. Verified on a VIO.
  - A crash on exit while video played.
  - HEARTBEAT was being matched as a command's answer.
- **Changed for 3.1.8.21:**
  - Modes use the attitude flags.
  - The control-source switch (`GB_FW_FLAG`).
  - Rates are sent in real units.
  - Rate and angle commands keep the reported mode.
  - Move to angle steers by rate.
  - All verified on a VIO.

## 2026-10-22: First release

- Connection management with saved profiles and an observed-only link status.
- MAVLink inspector: every TX/RX frame from the SDK's own socket, CRC check, decoding, filters, hex view, `.tlog` /
  `.csv` / `.jsonl` export, and libpcap network capture.
- VIO gimbal and camera control, telemetry, command and ACK tracking, RTSP video.
- Install rules, `scripts/build.sh`, and the acceptance checklist.
- The station UI: one Operate workspace (gimbal | video | camera), Analyze, a collapsible monitor and a SETTINGS drawer.
- Checked on a VIO (3.1.4.18): 59/59 control checks passed.
