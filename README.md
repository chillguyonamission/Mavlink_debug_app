# Gremsy MAVLink Debug App

A desktop station for Gremsy payloads (C++17, Qt 6, CMake). It controls the gimbal and camera through Gremsy's official
PayloadSDK, plays the payload's video, and shows every MAVLink frame on that link, so you can see exactly what was sent
and what came back. It runs on Linux and Windows and also works as a diagnostic tool for a faulty payload.

![Operate view](docs/screenshots/operate.png)

## What it does

| Area | Features |
|---|---|
| **Operate** | Gimbal: modes, control source, hold-to-move pad with a speed slider, move to angle, measured attitude. Camera: photo, recording, view and record source, IR palette, zoom and focus, object tracking (click the video), object detection, IR FFC and white balance, all camera settings in a panel under the video. Video: RTSP, or RTP/TS with KLV metadata, with a live overlay. |
| **Analyze** | Command log with ACK states and latency, telemetry and the payload's parameter streams, gimbal settings, traffic statistics, KLV, gamepad configuration. |
| **Diagnose** | Health check of the link and every payload part, a read-only network check, SDK update check, an exportable report, and the payload's own web settings. |
| **MAVLink monitor** | Every TX/RX frame from the SDK's own socket, CRC-checked and decoded, with filters, hex view, and `.tlog` / `.csv` / `.jsonl` export. Optional network capture shows other devices' traffic (for example payload ↔ autopilot). |
| **Gamepad** | An Xbox controller drives the gimbal and camera. Every button and axis can be reassigned, following Gremsy's gamepad configuration page. |

The user guide explains each screen: [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

## Status

**Branch `diagnostic-tool`, 2026-11-20.** 15/15 hardware-free tests pass on Linux and Windows CI.

| | Run on a real payload | Tests only |
|---|---|---|
| **Control** | VIO: connection, gimbal modes and rate, move to angle, zoom, focus, view source, tracking, camera settings, gamepad (fixed Xbox layout) | Gimbal OFF / RESET / MAPPING, photo, recording, gamepad remapping |
| **Video** | RTSP (H.264 720p, about 30 fps); RTP/TS with KLV received live once (VIO 3.1.9.24, UDP unicast) | KLV tag coverage beyond that one session |
| **Newer features** | None yet | Object detection, stream and FOV telemetry, FFC, white balance, gimbal settings, bitrate, DIAGNOSE view |
| **Models** | VIO (software 3.1.4.18, 3.1.8.21, 3.1.9.24) | ZIO, MB1 (Lynx, GHadron), OrusL: built from their SDK headers |
| **Platforms** | Ubuntu 24.04 (native and WSL2) | Windows: builds and passes CI; not yet run on a Windows PC with a payload |

What each check found is in [docs/HARDWARE_RESULTS.md](docs/HARDWARE_RESULTS.md). What is still open is in
[docs/DEVELOPMENT_PROGRESS.md](docs/DEVELOPMENT_PROGRESS.md).

## Progress

| Date | Milestone |
|---|---|
| 2026-10-22 | First release: connection and profiles, MAVLink inspector, network capture, VIO gimbal and camera control, telemetry, command tracking, RTSP video. Checked on a VIO. Station UI redesign. |
| 2026-10-23 | Tracking fixed and verified; speed slider; camera settings dialog. VIO updated to 3.1.8.21: modes moved to the flag route, move to angle steered by rate. ZIO, MB1 and OrusL profiles. |
| 2026-11-02 | Object detection with boxes on the video, stream and FOV telemetry, IR FFC, white balance, gimbal settings. Profiles audited against the SDK. |
| 2026-11-03 | RTP/TS video with MISB ST 0601 KLV metadata. |
| 2026-11-04 | Stream bitrate presets; camera settings moved under the video. |
| 2026-11-13 | PayloadSDK updated to `2021d9f`. Windows port and CI. DIAGNOSE view. Portable Windows zip and Linux AppImage. |
| 2026-11-20 | Gamepad control (Xbox Series X, verified on a VIO) and Gremsy's gamepad configuration page. |

Every change is listed in [CHANGELOG.md](CHANGELOG.md).

## Quick start

**Ubuntu 24.04** (the full guide is [docs/INSTALL_UBUNTU.md](docs/INSTALL_UBUNTU.md)):

```bash
git clone --recursive https://github.com/chillguyonamission/Mavlink_debug_app.git
cd Mavlink_debug_app
scripts/build.sh --run    # checks dependencies, builds, runs the tests, starts the app
```

- `scripts/build.sh` prints the `apt install` line for anything missing.
- On a machine short of memory, use `JOBS=2 scripts/build.sh`.
- Install: `cmake --install build --prefix ~/.local`.

**Windows:** download the portable zip from the latest CI run (Actions → build → Artifacts → `windows-portable`) and run
`mavlink_debug_app.exe`. See [docs/INSTALL_WINDOWS.md](docs/INSTALL_WINDOWS.md). CI also builds a Linux AppImage.

**Connect:** open **SETTINGS**, enter the payload address (default `192.168.55.1:14566`) and press **Connect**.
Connecting sends no movement or camera command. Video is at `rtsp://<IP>:8554/payload`.

## Documentation

| Document | Read it for |
|---|---|
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | Every screen and control, command states, the inspector, video, gamepad |
| [docs/HARDWARE_RESULTS.md](docs/HARDWARE_RESULTS.md) | What was measured on a real VIO, and its firmware quirks |
| [docs/DEVELOPMENT_PROGRESS.md](docs/DEVELOPMENT_PROGRESS.md) | Feature status, acceptance checklist, open hardware checks, known limitations, test log |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Source layout, design decisions, SDK patches, threads and data paths |
| [docs/INSTALL_UBUNTU.md](docs/INSTALL_UBUNTU.md), [docs/INSTALL_WINDOWS.md](docs/INSTALL_WINDOWS.md) | Install, connect a VIO, run the hardware checks |
| [docs/TESTING.md](docs/TESTING.md) | The test suite, soak and memory checks, troubleshooting |
| [docs/SDK_FEATURE_MATRIX.md](docs/SDK_FEATURE_MATRIX.md) | Every PayloadSDK API and model, and what the app covers |
| [docs/API_INTEGRATION.md](docs/API_INTEGRATION.md) | Exact calls behind the newer features, and what is not built and why |
| [docs/KLV_STREAM.md](docs/KLV_STREAM.md) | RTP/TS settings and the KLV tags |
| [CHANGELOG.md](CHANGELOG.md) | Changes by date |

## Licence

The PayloadSDK (`third_party/PayloadSdk`, a submodule) is GPL-3.0, so distributing this app requires releasing it under
GPL-3.0.
