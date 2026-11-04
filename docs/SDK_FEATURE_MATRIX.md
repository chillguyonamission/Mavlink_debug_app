# PayloadSDK feature matrix

Part 1 covers payload models. Part 2 covers every public `PayloadSdkInterface` API in `fba3c70`. Only VIO has run on
hardware.

## Part 1: payload coverage

### What the SDK supports

The SDK picks a model at build time, with one flag and one header per model (`-DVIO`, `-DZIO`, `-DMB1`, `-DORUSL`, and
`libs/payload-define/<model>_sdk.h`). **There are four targets**, and a product that shares a target has no header of
its own. `everySdkModelHeaderHasAProfile` in `tests/test_payload_control.cpp` fails if an SDK update adds a header
without a profile here.

| Product | SDK target | Source | App profile |
|---|---|---|---|
| VIO | `VIO` | SDK README, header | VIO |
| ZIO | `ZIO` | Build flag and header. Missing from the README's compatible list, and Gremsy's Python SDK page says "Limited Support" | ZIO |
| OrusL (ORUS-L) | `ORUSL` | SDK README, header | OrusL |
| Lynx | `MB1` | SDK README; a contributor on Gremsy/PayloadSdk issue 38: "build the SDK for Lynx by cmake -DMB1=1" | MB1 |
| GHadron ("GHardron" in the SDK) | `MB1` | SDK README. Gremsy's Python SDK page says "Limited Support" | MB1 |
| Plain Orus | none | The SDK has only `ORUSL` | none: not supported by this SDK |
| Other "small payloads" | none yet | Issue 38 says MB1 will be used for them "in the future" | none |

MB1 is the mainboard, not a product. Lynx and GHadron get no separate profiles because the SDK gives them no separate
IDs or values, and a profile that only renames MB1 would claim a difference nothing documents. The app accepts their
names when the payload reports them, and the Model box says to use MB1 for them.

Other differences inside a target:

- **IR core (F1 or G1):** the headers name each palette value differently per core, so the app shows both names. The core
  arrives in the `IR_TYPE` stream, but the app does not use it to pick a name.
- **Software version:** the SDK README asks for v2.0.0 or higher. The gimbal mode route changed at v307.54 (SDK comment).
  The app was checked on VIO 3.1.4.18 and 3.1.8.21 only, and 3.1.8.21 changed gimbal modes, control source and angle
  commands. Other models may have similar differences that nothing documents.

### Coverage matrix

| Payload model | Profile exists | SDK integration | MAVLink integration | Gimbal control | Camera features | Tracking | Video streaming | Status |
|---|---|---|---|---|---|---|---|---|
| VIO | Yes | Hardware-checked | Hardware-checked | Hardware-checked on 3.1.8.21: modes, rate, angle by rate | Zoom, focus, settings: hardware-checked. Photo, recording, FFC, white balance: mocks only | Hardware-checked on 3.1.4.18, not since 3.1.8.21. Detection: mocks only | Hardware-checked over TCP. UDP unresolved | **PARTIAL** |
| ZIO | Yes | Mocks only | Mocks only | Same code as VIO, unchecked. No control source in the header | 25 settings from the header. No IR, record source, FFC | Offered. The SDK builds its detection-mode example for ZIO but not its box example | Same URL as ui_demo, unchecked. The SDK builds no ui_demo for ZIO | **NEEDS VERIFICATION** |
| OrusL | Yes | Mocks only | Mocks only | Same code as VIO, unchecked. No control source in the header | 38 settings from the header. SpotAE not built | Offered, unchecked | Same URL as ui_demo, unchecked | **NEEDS VERIFICATION** |
| MB1 (Lynx, GHadron) | Yes | Mocks only | Mocks only | Same code as VIO, unchecked. `GB_FW_FLAG` Overwrite / Forward | 60 settings from the header. No focus. FFC offered on thin evidence | Offered, unchecked. `DETECTION_EN` may also be needed | Same URL as ui_demo, unchecked | **NEEDS VERIFICATION** |
| Plain Orus | No | None | None | None | None | None | None | **NOT SUPPORTED** by this SDK |

No model is **COMPLETE**, since that requires verification and even VIO has features only mocks have run. No model is
**MISSING**.

"Same code as VIO" is a fair description of what runs, not evidence that it works: no ZIO, MB1 or OrusL has run this app.

### What the SDK shows per model

The SDK has few runtime checks per model, so these are its only signals: which examples it builds, its `#if` branches, and
its `ui_demo`.

| Capability | VIO, OrusL | ZIO | MB1 | App |
|---|---|---|---|---|
| Focus | Yes | Yes | **No**: "not available for MB1", no example | Disabled on MB1 |
| IR FFC | Example | No IR | No example, but `ui_demo` shows the control | Off on ZIO. On for MB1: **needs verification** |
| One-push white balance | Example | Header value, no example | No header value | Off on MB1. On for ZIO: **needs verification** |
| View and record source | Yes | `#ifndef ZIO` on every record source and most view source calls | Yes | ZIO: one EO view, no record source |
| Box metadata | Example | Not in its example list | Example | Offered on all. ZIO: **needs verification** |
| FOV | Example | Not in its example list | Example | EO only on ZIO |
| Gimbal settings write | Example | Example | Read example only | Offered on all. MB1: **needs verification** |
| `SETTING_TARGET`, "which device to configure" | None | None | `ui_demo` offers it | **Not offered**: what it does to other settings is undocumented |
| Storage before capture | None | None | Capture example sets `STORAGE` first | Both IDs offered, but capture does not set them |

### What each profile leaves out

Checked by comparing every ID and numeric value in each header with its profile.

| Model | Not offered | Why |
|---|---|---|
| All | `RC_MODE` | Persistent, and not approved |
| VIO | `C_V_GAIN_HS/_LS`, `C_V_BrP_HS/_LS` | A VIO on 3.1.4.18 rejects them and lists `C_V_GAIN`, `C_V_BrP` instead |
| OrusL | `C_V_MinSP` | The header has no values for it. Gain IDs are empty strings |
| MB1 | `C_G_AE_MODE`, `C_G_SHUTTER`, `C_G_IRIS`, `C_G_GAIN`, `C_V_ZOOM_SPEED`, `C_V_FOCUS_SPEED`, `LRF_MODE` | The header has no values for them |
| MB1 | `C_G_AE_COMP`, `C_T_RG*_TMP` | Signed; settings are sent as uint32 |
| MB1 | `SETTING_TARGET` | See above |

### Runtime switching and persistence

The Model box can change at any time, including while connected, and rebuilds the gimbal and camera pages. The model is
saved in `conn/model` and with each connection profile. Stored values are validated on load, and an unknown model name
falls back to VIO. Parameter streams asked for on connect are not re-asked after a switch: press **Request payload info
and streams**.

## Part 2: SDK APIs

| Label | Meaning |
|---|---|
| **Implemented** | In the app, with tests |
| **Partial** | Part of it is in the app |
| **Available, unused** | The SDK has it; the app does not call it, by choice |
| **Needs verification** | Not exposed until hardware or documentation settles a stated question |
| **Unsupported** | Unusable as provided |

"HW" says whether the app's use of it ran on a real VIO: yes, or no.

## Connection and transport

| API | Status | HW | Notes |
|---|---|---|---|
| `sdkInitConnection`, `sdkQuit` | Implemented | yes | Patched `sdkQuit` joins its threads |
| `regTransportObserver` | Implemented | yes | App patch: feeds the inspector, tracker and telemetry |
| `checkPayloadConnection` | Unsupported | | Blocks forever. Link state comes from observed traffic |
| `getNewMessage`, `getNewMewssage`, `all_threads_init`, `payload_recv_handle`, `payload_request_handle`, the `_handle_*` methods | Available, unused | | SDK internals |
| `regPayloadInfoChanged`, `regPayloadStatusChanged`, `regPayloadParamChanged`, `regPayloadStreamChanged`, `regPayloadRecordInfoChanged`, `regPayloadHeartbeatChanged` | Available, unused | | The app reads the same messages from validated frames |

## Object detection and tracking

| API | Status | HW | Notes |
|---|---|---|---|
| `regPayloadDetectionChanged` | Implemented | no | Boxes on the video, status, selection |
| `sendPayloadRequestStreamRate(248, ms)` | Implemented | no | Detection stream |
| `setPayloadCameraParam(TRACK_MODE, ...)` | Implemented | tracking value: yes. Detection value: no | |
| `setPayloadObjectTrackingMode`, `setPayloadObjectTrackingPosition` | Implemented | yes, on 3.1.4.18 | Manual tracking |
| Selected box → tracker target | Needs verification | no | See `docs/API_INTEGRATION.md` |

## Camera

| API | Status | HW | Notes |
|---|---|---|---|
| `setPayloadCameraParam`, `getPayloadCameraSettingList`, `getPayloadCameraSettingByID` | Implemented | yes | Settings dialog |
| `getPayloadCameraSettingByIndex` | Available, unused | | The app reads by ID |
| `setCameraZoom`, `setCameraFocus` | Implemented | yes | |
| `setCameraZoomTarget` | Needs verification | | Range not documented per model |
| `setPayloadCameraMode` | Implemented | yes | |
| `setPayloadCameraCaptureImage`, `setPayloadCameraRecordVideoStart`, `setPayloadCameraRecordVideoStop` | Implemented | no | SD writes not yet approved for a hardware run |
| `setPayloadCameraStopImage` | Available, unused | | The app takes single shots |
| `getPayloadCameraMode`, `getPayloadStorage`, `getPayloadCaptureStatus`, `getPayloadCameraInformation`, `getPayloadComponentBasicInformation` | Implemented | yes | |
| `getPayloadCameraStreamingInformation`, `getPayloadCameraFOVStatus` | Implemented | no | Read-only, Analyze › Telemetry. ZIO has no IR, so the app does not ask it for IR FOV |
| `setPayloadCameraFFCMode`, `setPayloadCameraFFCTrigg` | Implemented | no | VIO, MB1, OrusL (ZIO has no IR). Operate › IMAGE CALIBRATION |
| `setPayloadCameraWBOnePushTrigg` | Implemented | no | VIO, ZIO, OrusL (`mb1_sdk.h` has no one-push value) |
| `getPayloadCameraFFCMode` | Unsupported | | Empty body: it returns nothing |
| `setCameraExtSettings_SpotAE_*` | Needs verification | | OrusL only; no unit to check on |

## Stream

| API | Status | HW | Notes |
|---|---|---|---|
| `setPayloadStreamBitrate` | Implemented | no | Persistence and video recovery unverified. See `API_INTEGRATION.md` |
| `setPayloadStreamResolution`, `setPayloadStreamProfile` | Needs verification | | Values, persistence and RTSP recovery unverified |
| `getPayloadStreamBitrate` | Unsupported | | Requests stream information and always returns 0. The app shows the bitrate from the received `VIDEO_STREAM_INFORMATION` instead |

## Gimbal

| API | Status | HW | Notes |
|---|---|---|---|
| `setGimbalMode`, `setGimbalSpeed`, `setGimbalDeviceFlags` | Implemented | yes | `setGimbalDeviceFlags` is an app patch. OFF, RESET and MAPPING not run on hardware |
| `getGimbalDeviceStatusFlags`, `setGimbalMove_MAVLinkV1` | Available, unused | | The app reads flags from validated telemetry |
| `getPayloadGimbalSettingList`, `getPayloadGimbalSettingByID`, `setPayloadGimbalParamByID` | Implemented | no | Analyze › Gimbal settings. 16-character IDs are refused: the SDK's `strcpy` overruns |
| `getPayloadGimbalSettingByIndex` | Available, unused | | Reading again fills gaps |
| `sendPayloadGimbalCalibGyro`, `sendPayloadGimbalCalibAccel`, `sendPayloadGimbalSearchHome`, `sendPayloadGimbalAutoTune` | Available, unused | | Not requested |
| `sendPayloadGimbalCalibMotor` | Needs verification | | Sends `MAV_CMD_DO_SET_HOME` with param7 = 1, which does not match its name |

## Payload system and parameter streams

| API | Status | HW | Notes |
|---|---|---|---|
| `setParamRate` | Implemented | yes | DEBUG parameter streams |
| `requestParamValue`, `requestMessageStreamInterval` | Available, unused | | The SDK sends its own stream requests |
| `setPayloadStandbyMode`, `setPayloadRestartApps` | Needs verification | | Disruptive; recovery not verified |
| `sendPayloadGPSPosition`, `sendPayloadGPSRawInt`, `sendPayloadSystemTime` | Needs verification | | The app has no autopilot source to forward |

## Part 3: SDK examples with no matching app feature

Everything else the examples show is in the tables above.

| Example | What it does | Why it is not in the app |
|---|---|---|
| `payload_download_media_files`, `payload_extract_tiff_file` | Fetch and unpack files from the payload | Not requested. Needs libcurl and JSON |
| `tests/parse_klv_stream`, `tests/parse_thermal_stream` | Read KLV metadata and thermal data from the video | Not requested. Needs the SDK's MISB library and OpenCV |
| `camera_time_lapse_photography` | Interval capture | Not requested. The app takes single shots |
| `camera_ir_isotherm_profile` | Isotherm regions | On MB1, in the settings panel. Its IDs are in no VIO or OrusL header, so not offered there |
| `gimbal_do_calib`, `gimbal_calib_auto_tune` | Gimbal calibration | Not requested, and disruptive |
| `payload_get_video_record_status` | Record status callback | The app reads `CAMERA_CAPTURE_STATUS` instead. Its file URL is not shown |
| `tests/gamepad_control` | Gamepad control | Not requested |
