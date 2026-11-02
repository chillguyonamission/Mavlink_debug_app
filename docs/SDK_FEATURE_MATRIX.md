# PayloadSDK feature matrix

Every public `PayloadSdkInterface` API in `fba3c70`, and what this app does with it. Only VIO has run on hardware.

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
| `getNewMessage`, `all_threads_init`, `payload_recv_handle` | Available, unused | | SDK internals |
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
| `getPayloadCameraStreamingInformation`, `getPayloadCameraFOVStatus` | Implemented | no | Read-only, Analyze › Telemetry. ZIO has no IR, so its IR request should go unanswered |
| `setPayloadCameraFFCMode`, `setPayloadCameraFFCTrigg` | Implemented | no | VIO, MB1, OrusL (ZIO has no IR). Operate › IMAGE CALIBRATION |
| `setPayloadCameraWBOnePushTrigg` | Implemented | no | VIO, ZIO, OrusL (`mb1_sdk.h` has no one-push value) |
| `getPayloadCameraFFCMode` | Unsupported | | Empty body: it returns nothing |
| `setCameraExtSettings_SpotAE_*` | Needs verification | | OrusL only; no unit to check on |

## Stream

| API | Status | HW | Notes |
|---|---|---|---|
| `setPayloadStreamBitrate`, `setPayloadStreamResolution`, `setPayloadStreamProfile` | Needs verification | | |
| `getPayloadStreamBitrate` | Unsupported | | Requests stream information and always returns 0. The app shows the bitrate from the received `VIDEO_STREAM_INFORMATION` instead |

## Gimbal

| API | Status | HW | Notes |
|---|---|---|---|
| `setGimbalMode`, `setGimbalSpeed`, `setGimbalDeviceFlags` | Implemented | yes | `setGimbalDeviceFlags` is an app patch. OFF, RESET and MAPPING not run on hardware |
| `getGimbalDeviceStatusFlags`, `setGimbalMove_MAVLinkV1` | Available, unused | | The app reads flags from validated telemetry |
| `getPayloadGimbalSettingList`, `getPayloadGimbalSettingByID`, `getPayloadGimbalSettingByIndex`, `setPayloadGimbalParamByID` | Available, unused | | |
| `sendPayloadGimbalCalibGyro`, `sendPayloadGimbalCalibAccel`, `sendPayloadGimbalSearchHome`, `sendPayloadGimbalAutoTune` | Available, unused | | Not requested |
| `sendPayloadGimbalCalibMotor` | Needs verification | | Sends `MAV_CMD_DO_SET_HOME` with param7 = 1, which does not match its name |

## Payload system and parameter streams

| API | Status | HW | Notes |
|---|---|---|---|
| `setParamRate` | Implemented | yes | DEBUG parameter streams |
| `requestParamValue`, `requestMessageStreamInterval` | Available, unused | | The SDK sends its own stream requests |
| `setPayloadStandbyMode`, `setPayloadRestartApps` | Needs verification | | Disruptive; recovery not verified |
| `sendPayloadGPSPosition`, `sendPayloadGPSRawInt`, `sendPayloadSystemTime` | Needs verification | | The app has no autopilot source to forward |
