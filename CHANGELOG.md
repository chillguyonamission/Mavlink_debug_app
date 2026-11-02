# Changelog

Newest first. "Tested" means the hardware-free CTest suite unless a line says hardware. See
`docs/DEVELOPMENT_PROGRESS.md` for what has and has not run on a real payload.

## Unreleased

### Added

- **Object detection:** enable and disable from Operate › OBJECT DETECTION, status with seven states, boxes painted
  on the existing video view, click to select a box by track ID. Data comes from the SDK's
  `regPayloadDetectionChanged()` callback. Tested with a fake payload; not run on hardware.
- `hw_vio_commands --steps detection,detection-forward`: built, not run.
- **Stream and FOV telemetry:** Analyze › Telemetry shows `VIDEO_STREAM_INFORMATION` and `CAMERA_FOV_STATUS`, only while
  a reply is fresh. Tested with a fake payload; not run on hardware.
- **Image calibration:** IR FFC auto, manual and trigger, and EO one-push white balance, offered only on models whose
  SDK header supports them. Tested with a fake payload; not run on hardware.

### Not added, on purpose

- Forwarding a selected box to the tracker: no source confirms it works on any model. See
  `docs/API_INTEGRATION.md`.
