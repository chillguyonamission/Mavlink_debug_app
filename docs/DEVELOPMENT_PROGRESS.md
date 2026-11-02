# Development progress

Software tests and hardware validation are tracked separately. A passing test here means a fake payload on
loopback, never a real unit.

## Status by phase

| Phase | State | Software tests | Hardware |
|---|---|---|---|
| Object detection and bounding boxes | Implemented; forwarding to the tracker held back | `detection`, `main_window` | **Not run** |
| 1. Stream and FOV telemetry | Implemented | `payload_control`, `main_window` | **Not run** |

## Hardware validation still needed

Each of these needs your go-ahead, because it changes payload state.

| Check | Command | What it answers |
|---|---|---|
| Detection on a VIO | `hw_vio_commands --ip <IP> --steps detection` (TRACK_MODE changes, then is restored; nothing moves) | Do packets arrive? Do real boxes pass validation? Does rate 0 stop them? |
| Forwarding | `hw_vio_commands --ip <IP> --steps detection-forward` (**the gimbal follows**, then returns) | Does a detected box work as a tracker target after TRACK_MODE returns to tracking? |
| Overlay | Operate, with video and an object in view | Do boxes sit on the objects, at the stream's real resolution? |
| Stream and FOV | Open Analyze › Telemetry (read-only) | Does the payload answer? Which `camera_device_id` does each FOV reply carry? |

## Test log

Run on 2026-11-02, native Ubuntu, build directory outside the repository (see `docs/TESTING.md`).

| Point | Result |
|---|---|
| Before any change | 9/9 passed |
| After object detection | 10/10 passed, three runs in a row |
| After phase 1 | 10/10 passed |
