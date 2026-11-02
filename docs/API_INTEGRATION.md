# API integration reference

Each feature lists the exact call, what goes on the wire, and what counts as an answer. Source: PayloadSDK
`fba3c70` (`SDK_VERSION` 3.2.0_build.15082026). SDK methods return `void`, so calling one proves nothing: Analyze ›
Commands shows the observed frame and the answer separately.

## Object detection

Three kinds of thing are involved: **Gremsy SDK API** (`regPayloadDetectionChanged`, `sendPayloadRequestStreamRate`,
`setPayloadCameraParam`), **standard MAVLink** (`MAV_CMD_SET_MESSAGE_INTERVAL`, `V2_EXTENSION`), and **application
side** (validation, states, overlay, selection). The packet format inside `V2_EXTENSION` is Gremsy's.

### Commands

| Action | App call | SDK call | On the wire | Answer |
|---|---|---|---|---|
| Enable, step 1 | `PayloadLink::detectionEnable()` | `setPayloadCameraParam("TRACK_MODE", 1, PARAM_TYPE_UINT32)` | `PARAM_EXT_SET`, followed by a read-back `PARAM_EXT_REQUEST_READ` | `PARAM_EXT_ACK` |
| Enable, step 2, 500 ms later (the example's delay) | same | `sendPayloadRequestStreamRate(248, 100)` | `COMMAND_LONG` `MAV_CMD_SET_MESSAGE_INTERVAL`, param1 = 248, param2 = 100000, to compid 26 | `COMMAND_ACK`, then the packets |
| Stop stream | `detectionStop()` | `sendPayloadRequestStreamRate(248, 0)` | as above, param2 = 0 | `COMMAND_ACK`, then silence |
| Disable (button) | `detectionStop()`, then `cameraParam()` | as above, then `setPayloadCameraParam("TRACK_MODE", 0, ...)` | as above, then `PARAM_EXT_SET` | `PARAM_EXT_ACK` |

The SDK comment says rate 0 stops the stream. Standard MAVLink defines param2 = 0 as "default rate" and -1 as
"disabled", so whether 0 stops it is firmware behaviour. `hw_vio_commands --steps detection` checks it.

`TRACK_MODE` has the same ID and values in `vio_sdk.h`, `zio_sdk.h`, `mb1_sdk.h` and `orusl_sdk.h`: 0 tracking,
1 detection.

### Packet

`V2_EXTENSION` with `message_type` 0x4001, payload `det_packet_t` (`libs/detection_packet.h`): magic `0x4744`,
version 1, `num_boxes`, `frame_id`, then up to 20 boxes of 12 bytes.

| Field | Meaning |
|---|---|
| `x`, `y` | Top-left corner, in 1920×1080 reference pixels |
| `w`, `h` | Size, same space |
| `track_id` | 0 = no track assigned yet |
| `class_id` | Depends on the model loaded on the payload. No names are shown. The SDK example's COCO table applies only to its EO model, and it warns that IR IDs differ |
| `confidence` | 0–255 = 0–100% |

The packet has no timestamp and no video frame reference.

### Validation

| Layer | Check |
|---|---|
| App frame parser (for the inspector) and SDK parser | MAVLink CRC with the V2_EXTENSION CRC extra |
| SDK `_handle_msg_v2_extension()` | `message_type`, magic, version, clamps `num_boxes` to 20 |
| `parseDetectionPacket()` | Rejects a box with zero width or height, an origin outside the frame, or a size above the frame's. Clips one that only overruns the edge. Counts rejected boxes, including any claimed above 20 |

**No payload-length check exists, and none is needed.** MAVLink 2 trims trailing zero bytes from every frame, and
`mavlink_msg_v2_extension_decode()` zero-fills them again. So:

- A short frame is normal: a packet whose last box ends in zeros is legitimately shorter, and rejecting it would drop
  good data.
- A box the wire did not carry reads as all zeros, so `w = h = 0` rejects it. A header cut short fails the magic or
  version check.
- The SDK's fixed `memcpy` copies 248 bytes from a 249-byte zero-filled array, so it cannot over-read.

The callback therefore loses nothing by not exposing the length, and no SDK patch was added. Tests cover each case
with real frames through the real SDK parser.

### States

| State | Meaning | Cause |
|---|---|---|
| Off | Support unknown | Not enabled from here, no packets |
| Waiting | | Enabled, no packet yet, under 5 s |
| No response | | Enabled, no packet after 5 s |
| Active, nothing detected | | Packets arriving with 0 valid boxes |
| Active | | Packets arriving with boxes |
| Stale | | Packets stopped for over 1.5 s (the example's `DET_TIMEOUT_MS`) while enabled. Boxes and selection are removed |
| Refused | | `TRACK_MODE` or the stream request got a rejecting ACK and no packets arrived |

An ACK timeout is not a refusal: it ends in No response. Packets arriving without an enable from this app count as
Active, because that is what the payload is doing.

### Selection and the tracker

- A click on a box selects it by `track_id`, only while the tracker is not armed. Selecting sends nothing. Boxes with
  `track_id` 0 cannot be selected, because nothing identifies them across packets.
- The panel shows the box's centre and size, which is what `trackingTarget()` would take. A box's `x, y` is the
  top-left corner, `trackingTarget()` takes the centre, and the `TRK_POS_*` the tracker reports is top-left again.
- `trackingTarget()` takes coordinates and a size. It has no way to name a `track_id`.

**Forwarding to the tracker is not implemented.** What the sources establish:

| Question | Evidence | Answer |
|---|---|---|
| Can detection and tracking run together? | One parameter with two values in all four headers. `demo_project` turns detection off by setting tracking mode | Treated as exclusive; not proven by hardware |
| Does the tracker accept a target after switching back? | Nothing in the SDK | **Unknown**: `--steps detection-forward` tests it on a VIO |
| Are target semantics the same per model? | Centre and size verified on VIO 3.1.4.18 only. Tracking was last run before 3.1.8.21 | **Unknown** for ZIO, MB1, OrusL, and for VIO 3.1.8.21 |

To keep the two apart, **Select target** stops the detection stream before its unchanged sequence, and **Enable
detection** first stops an armed tracker, so a later click cannot send a target by accident.

### Limits

- Only the EO view is used by the SDK's example. In other views (IR, picture-in-picture, side by side) whether the
  coordinates fit the picture is unknown; the panel warns when the payload reports another view.
- Disconnecting leaves `TRACK_MODE` and the stream as they were on the payload, the same way tracking is left.
- The firmware needed for detection is not documented anywhere in the SDK.

## Stream and FOV telemetry

Read-only. **SDK API** for the requests, **standard MAVLink** for the messages, **application side** for the display.

| Action | App call | SDK call | On the wire | Answer |
|---|---|---|---|---|
| Stream | `requestStreamAndFov()` | `getPayloadCameraStreamingInformation()` | `COMMAND_LONG` `MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION`, param1 = 0 (all), to the camera | `VIDEO_STREAM_INFORMATION` |
| FOV | same | `getPayloadCameraFOVStatus(CAMERA_EO)`, `(CAMERA_IR)` | `COMMAND_LONG` `MAV_CMD_REQUEST_MESSAGE`, param1 = 271, param2 = 0 or 1, to the camera | `CAMERA_FOV_STATUS` |

- **Workflow:** the button sends tracked requests. While Analyze › Telemetry is open, `pollStreamAndFov()` repeats them
  every 2 s without adding to the command log, once the camera has been heard (before that the SDK addresses the wrong
  component). The SDK's own FOV example polls at 2 Hz.
- **Response and ACK are separate:** the requested message gives the state *Answered*; a `COMMAND_ACK` is recorded on
  top. One reply answers one request. An unanswered request ends in *No ACK (timeout)*.
- **Display:** values come from CRC-valid received frames. They show only while the newest reply is younger than the link
  timeout (at least 3 s, since replies come at most every 2 s). After that only the age is shown.
- **Bitrate:** taken from the received message. `getPayloadStreamBitrate()` is not used, since it always returns 0.
- **Not assumed:** `param2` for FOV is a Gremsy use of a standard field. How `camera_device_id` in the reply maps to EO and
  IR is unverified, so the raw number is shown. MAVLink defines 0 as "a MAVLink camera", so EO and IR replies could share
  an ID, in which case the newer one replaces the older.
