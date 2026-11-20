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
| `class_id` | Depends on the model loaded on the payload. The video overlay shows the number only. The data window adds a name marked as a guess: the SDK example's COCO table applies only to its EO model, and it warns that IR IDs differ |
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

### Data window

Application side, read-only: the **Detection data…** button in Operate › OBJECT DETECTION opens a separate, non-modal
window. It sends nothing.

| Part | Content |
|---|---|
| State | The same state as the panel. Stays live while paused |
| Summary | Frame ID, valid and claimed boxes, age of the last packet, packets per second over the last 2 s |
| Newest packet | Track, class, confidence, x, y, w, h, centre. Empties when packets go stale |
| Log | Last 1000 packets, newest first: receive time, frame ID, box count, invalid count |
| Pause | Freezes the table and log. Packets keep being recorded and appear on Resume, up to the 1000 limit |

- **Receive time** is this PC's clock when the SDK delivered the packet, because the packet has no timestamp.
- **Class:** `3 (motorcycle?)` is the number plus a guess from the SDK example's COCO table. An ID above 79 shows only the
  number. The guess is yours to judge against the picture: it can be wrong on IR or on another model.
- **Invalid boxes** appear only as a count, not as rows.
- The log is cleared on connecting. It stays readable after a disconnect while the window is open; the button itself is
  disabled while disconnected, like the rest of the panel.

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
- **EO-only models:** the IR request is not sent, so ZIO asks only for EO.
- **Not assumed:** `param2` for FOV is a Gremsy use of a standard field. How `camera_device_id` in the reply maps to EO and
  IR is unverified, so the raw number is shown. MAVLink defines 0 as "a MAVLink camera", so EO and IR replies could share
  an ID, in which case the newer one replaces the older.

## Image calibration

Custom Gremsy commands. All are `COMMAND_LONG` `MAV_CMD_USER_4` to the payload core (compid 26), with the unused
params 0.

| Control | App call | SDK call | param1 | param2 | param3 |
|---|---|---|---|---|---|
| FFC auto | `irFfcMode(true)` | `setPayloadCameraFFCMode(FFC_MODE_AUTO)` | 2 | 6 | 1 |
| FFC manual | `irFfcMode(false)` | `setPayloadCameraFFCMode(FFC_MODE_MANUAL)` | 2 | 6 | 0 |
| Trigger FFC | `irFfcTrigger()` | `setPayloadCameraFFCTrigg()` | 2 | 7 | 0 |
| WB one push | `eoWbOnePush()` | `setPayloadCameraWBOnePushTrigg()` | 2 | 8 | 0 |

- **Feedback:** `COMMAND_ACK` in Analyze › Commands. `COMMAND_ACK` names only the `MAV_CMD`, so answers to several
  `USER_4` commands in flight are matched oldest first and cannot be told apart. Send them one at a time when checking
  a result. An accepted ACK says the command was accepted, not that the picture changed.
- **FFC mode read-back:** `getPayloadCameraFFCMode()` has an empty body and is not called. `irFfcMode()` starts the
  `FFC_MODE` DEBUG stream (`setParamRate(PARAM_CAM_IR_FFC_MODE, 1000)`), shown under *Payload reports*. There is no
  read-back for a trigger or for one-push.
- **Model support:** FFC needs an IR camera, so ZIO has it disabled. One-push is enabled where the header defines
  `PAYLOAD_CAMERA_VIDEO_WHITE_BALANCE_ONE_PUSH`, so MB1 has it disabled. The SDK itself gates neither command per model.
- **Not assumed:** the SDK's FFC example selects the IR view first; the app does not change the view. Whether one-push
  needs White balance = One push first is undocumented.

## Gimbal settings

The standard MAVLink parameter protocol, through the **SDK API**, on its own page: Analyze › Gimbal settings. It never
sends motion or mode commands.

| Action | App call | SDK call | On the wire | Answer |
|---|---|---|---|---|
| Read | `gimbalSettingsRead()` | `getPayloadGimbalSettingList()` | `PARAM_REQUEST_LIST` to the gimbal | One `PARAM_VALUE` per parameter |
| Write | `gimbalSettingWrite(id, value)` | `setPayloadGimbalParamByID()` | `PARAM_SET`: the ID, the value as a float, `param_type` 0 | No ACK exists |
| Read back | automatic after a write | `getPayloadGimbalSettingByID()` | `PARAM_REQUEST_READ`, `param_index` -1 | `PARAM_VALUE` |

- **Response format:** `PARAM_VALUE` has `param_id` (16 characters, not terminated at full length), `param_value` as a
  float, `param_type`, `param_index`, `param_count`. Only frames from a gimbal component (154, 171–175) are stored. The
  page shows *N of M received*, because replies can be lost. Reading again fills the gaps.
- **Feedback:** the command log says *Sent (no ACK defined)*. The only confirmation is the value in the table changing.
- **Guards on a write:**
  - The operator confirms a dialog naming the parameter, the new value and the reported one.
  - The ID must have been read from the gimbal.
  - The value must fit the reported type. It travels as a float, so integers beyond 2^24 and unknown types are refused.
  - A 16-character ID is refused, because `setPayloadGimbalParamByID()` copies it with `strcpy` into a `char[16]`. Those
    parameters can be read, not written.
  - The selection is held by ID, so a row moving while the list arrives cannot change what gets written.
- **Not known:** ranges (the SDK documents none; its example writes `STIFF_TILT` = 50), whether a value is stored across
  restarts, and which parameters each model has. The list comes from the gimbal, so no per-model table exists.
- **Not exposed:** `sendPayloadGimbalCalibMotor()` sends `MAV_CMD_DO_SET_HOME` with param7 = 1, which does not match its
  name. Calibration and auto-tune were not requested.

## Stream bitrate

| Control | App call | SDK call | On the wire | Answer |
|---|---|---|---|---|
| Operate › SETTINGS › EO bitrate, IR bitrate | `streamBitrate(cam, bps)` | `setPayloadStreamBitrate(cam, bps)` | `COMMAND_LONG` `MAV_CMD_USER_4` to compid 26: param1 = 4, param2 = 2, param3 = cam (1 EO, 2 IR), param4 = 0, param5 = bps | `COMMAND_ACK`; new value in `VIDEO_STREAM_INFORMATION` |

- **Values:** fixed presets from 0.512 to 16 Mbit/s. That range appears only in the SDK example's help text. A float holds every value exactly.
- **Confirming it:** the ACK says only that the command was accepted. Analyze › Telemetry shows the bitrate the payload reports.
- **Not known:** whether the value survives a restart, whether RTSP or RTP/TS keeps playing, and whether the IR value applies to the current
  view. The app does not restart video by itself: if the stream drops, it shows the error and waits for Play.
- Tested only against the fake payload.

## Not built

These stay out until the stated question is answered. The encodings below are from the SDK source; none has been sent by
this app. All are `COMMAND_LONG` `MAV_CMD_USER_4` to compid 26.

| Function | SDK call | param1, param2, param3, param4, param5 | Waiting for |
|---|---|---|---|
| Stream resolution | `setPayloadStreamResolution(cam, level)` | 4, 2, cam, 1, level | The level list (the example shows only level 1: EO 1920×1080, IR 640×512), persistence, recovery |
| Stream profile | `setPayloadStreamProfile(cam, profile)` | 4, 2, cam, 2, profile | The profile list (the example shows only 1 = Balance), persistence, recovery |
| Standby | `setPayloadStandbyMode(on)` | 4, 3, 0, on, 0 | What stops working, and how to recover. Disruptive |
| Restart an app | `setPayloadRestartApps(id)` | 4, 4, id, 0, 0 (0 payload, 1 streaming, 2 tracking, 3 web UI, 4 gimbal) | Link and video recovery per app. Disruptive |
| OrusL SpotAE | `setCameraExtSettings_SpotAE_*` | 4, 5, 0 display / 1 mode / 2 position, then values | An OrusL unit, and the value meanings |
| GPS, time | `sendPayloadGPSPosition`, `sendPayloadGPSRawInt`, `sendPayloadSystemTime` | Standard messages, not `USER_4` | An autopilot input source, which the app does not have. Sending invented positions would be worse than none |

The stream functions number cameras 1 = EO, 2 = IR, while `getPayloadCameraFOVStatus()` uses `camera_type_t`
0 = EO, 1 = IR, so the two must not share a constant.

The app already reads the current bitrate, resolution and profile-independent stream data from `VIDEO_STREAM_INFORMATION`
(see *Stream and FOV telemetry*), so a later resolution or profile write can be confirmed from a received message.
