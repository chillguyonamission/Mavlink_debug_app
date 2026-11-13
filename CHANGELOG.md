# Changelog

Newest first. "Tested" means the hardware-free CTest suite unless a line says hardware. See
`docs/DEVELOPMENT_PROGRESS.md` for what has and has not run on a real payload.

## Unreleased

### Added

- **DIAGNOSE view.** Health check (link, each payload part's heartbeat and state, decoded gimbal failure flags, model,
  payload value stream, frame and send errors, sequence gaps, failed commands), a read-only network check (PC
  adapters on the payload's subnet, its TCP services, its software list from the web app), an SDK update check
  against GitHub, and a text report. Tested with unit tests and a loopback network check; not yet on a payload.
- **Web settings tab:** the payload's web app inside the app (Qt WebEngine), or **Open in browser** without it. The
  pages were checked in Qt WebEngine on a VIO (Setting App 2.7.0.12) before this was built; the tab itself has not
  run against a payload yet.
- **Portable packages from CI:** Windows zip (Qt, GStreamer plugins, MAVLink XML) and a Linux AppImage.

- **Windows build (in progress).** The SDK is ported by `patches/payloadsdk-windows.patch`: Winsock instead of BSD sockets,
  `std::thread`/`std::mutex` instead of pthread, 1 ms timer resolution for its polling loops. UART control is Linux-only.
  Network capture on Windows needs the user to install Npcap (it may not be bundled); without it the capture tools say so.
  CI builds and tests on Ubuntu 24.04 (13/13) and Windows Server 2022 with MSVC (12/12; the libpcap test is Linux-only).
  KLV settings list Windows adapters by their system names (e.g. `ethernet_32769`); whether GStreamer's multicast
  interface option accepts them is untested. Nothing has run on a Windows PC with a payload yet.

### Changed

- **PayloadSDK** moved from `fba3c70` to `2021d9f` (23 upstream commits). The app's SDK patch applies unchanged. Read-only
  capture on a VIO (3.1.9.24) with the new SDK: no errors, the same answers as before.
- **Gimbal control source** now comes from each model's SDK header: `GB_FW_FLAG` Autopilot / Onboard computer / SBUS Convert
  on all four models. ZIO and OrusL gain the control-source switch; MB1's values were renamed upstream from Overwrite / Forward.
  Only VIO has run on hardware.

- **Camera settings:** now a panel under the video instead of a window on top of it, so the picture stays visible while adjusting. It takes the MAVLink monitor's place until closed.
  Tested with a fake payload; the panel has not been opened on hardware.

### Fixed

- After a payload reboot, EO/IR zoom, FFC mode and IR temperatures stopped updating and the rangefinder fell from 10 Hz to
  1 Hz: the payload forgets the streams it was asked for, and the app asked once per connection. It now asks again when the
  payload core's heartbeat returns after the link was stale. Found on a VIO by power-cycling it; tested with a fake payload.

- The mouse wheel over any combo box no longer changes it. Qt reports that as a user choice, so scrolling a column past
  one could send a command, such as the gimbal control source.

### Added

- **Stream bitrate:** EO and IR bitrate presets (0.512 to 16 Mbit/s) in Operate › SETTINGS, changed while connected. Tested with a fake
  payload; not run on hardware. See `docs/API_INTEGRATION.md`.
- **RTP/TS video + KLV:** a video source selector in Operate › LIVE VIDEO, alongside RTSP. Plays H.264 from an RTP/MPEG-TS stream and
  decodes MISB ST 0601 KLV from the same stream, with a compact panel under the video and a new Analyze › KLV tab. Decoder and
  model come from the KLV monitor project. Tested with a synthetic stream; not run on hardware. Metadata is the latest in the
  stream and is not synchronised to frames. Memory grows by about 20 MB per hour in this mode, from `avdec_h264`. See `docs/KLV_STREAM.md`.
- **Object detection:** enable and disable from Operate › OBJECT DETECTION, status with seven states, boxes painted
  on the existing video view, click to select a box by track ID. Data comes from the SDK's
  `regPayloadDetectionChanged()` callback. Tested with a fake payload; not run on hardware.
- **Detection data window:** the **Detection data…** button opens a separate window with the newest packet's boxes, a
  summary, Pause, and a log of the last 1000 packets. Class numbers carry a marked COCO guess in this window only. Tested
  with a fake payload; not run on hardware.
- `hw_vio_commands --steps detection,detection-forward`: built, not run.
- **Stream and FOV telemetry:** Analyze › Telemetry shows `VIDEO_STREAM_INFORMATION` and `CAMERA_FOV_STATUS`, only while
  a reply is fresh. Tested with a fake payload; not run on hardware.
- **Image calibration:** IR FFC auto, manual and trigger, and EO one-push white balance, offered only on models whose
  SDK header supports them. Tested with a fake payload; not run on hardware.
- **Gimbal settings:** a new Analyze tab reads the gimbal's parameters and writes one after a confirmation. Only
  parameters already read, and only values their type can hold. Tested with a fake payload; not run on hardware.

### Fixed

- **MB1:** focus buttons are disabled, since the SDK has no focus controls for MB1.
- **Wrong-model warning:** Lynx and GHadron on MB1, and spellings such as "ORUS-L", no longer warn. Only VIO's reported name
  has been seen on a real unit.
- **EO-only models:** ZIO is no longer asked for IR FOV, IR, rangefinder and view streams.
- **PAYLOAD lamp:** counts any MAVLink camera or gimbal ID, as the SDK does, not only 100 and 154.
- **Capture drop count:** resets on connect, so one overflow no longer warns forever.

### Changed

- FFC mode and IR temperature streams are requested on connect on IR models, as the SDK's `ui_demo` does. Tested with a fake
  payload; not run on hardware.
- `STORAGE_TYPE` added to the MB1 camera settings.
- Payload pages skip their refresh while hidden.

### Not added, on purpose

- KLV alongside RTSP video: the one capture shows both in one TS, so a metadata-only receiver has no verified use yet.
- Map view: the parser has no platform or target coordinates, and the station has no map framework.
- Frame-level KLV synchronisation: there is no PTS mapping.
- From the KLV monitor: QML screens, `VideoItem.h`, the A–G diagnosis, raw KLV and `.ts` recording, and PMT parsing through GStreamer's
  debug log, which would change logging for the whole process including RTSP.
- Forwarding a selected box to the tracker: no source confirms it works on any model.
- Stream resolution and profile; standby and restart; OrusL SpotAE; GPS and time forwarding: their conditions
  are not met.

- Separate Lynx or GHadron profiles: the SDK gives them no separate IDs or values.

Reasons for each are in `docs/API_INTEGRATION.md` and `docs/SDK_FEATURE_MATRIX.md`.
