# Changelog

Newest first. "Tested" means the hardware-free CTest suite unless a line says hardware. See
`docs/DEVELOPMENT_PROGRESS.md` for what has and has not run on a real payload.

## Unreleased

### Added

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

- Forwarding a selected box to the tracker: no source confirms it works on any model.
- Stream bitrate, resolution and profile; standby and restart; OrusL SpotAE; GPS and time forwarding: their conditions
  are not met.

- Separate Lynx or GHadron profiles: the SDK gives them no separate IDs or values.

Reasons for each are in `docs/API_INTEGRATION.md` and `docs/SDK_FEATURE_MATRIX.md`.
