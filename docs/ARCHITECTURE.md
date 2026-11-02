# Architecture

The module list is in `README.md` under *Layout*. This page covers how data moves and who owns what.

## Ownership

- `PayloadLink` is the only owner of `PayloadSdkInterface`, and `payload_link.cpp` is the only file that includes the
  SDK's header. All commands use the SDK's own UDP transport; the app opens no second command socket.
- `pcap_capture` is a separate observer. Its data never affects link health or command tracking.
- `VideoPlayer` is a separate RTSP pipeline, independent of the MAVLink link.

## Threads

| Thread | Runs | Rule |
|---|---|---|
| SDK I/O threads | `regTransportObserver()` callback | Copies the datagram into a bounded queue under `m_mutex` |
| SDK receive thread | `regPayloadDetectionChanged()` callback | Overwrites one latest-packet slot under `m_mutex` |
| GUI thread | `PayloadLink::poll()` every 100 ms | Takes both, parses, updates state, emits signals |
| GStreamer streaming thread | `VideoPlayer::onSample()` | Copies the newest frame; the GUI takes the latest |

No SDK or GStreamer thread touches a widget.

## Object detection path

```
payload ─V2_EXTENSION(248)─▶ SDK _handle_msg_v2_extension()      type, magic, version, num_boxes clamp
                              │ regPayloadDetectionChanged()     SDK receive thread
                              ▼
                         PayloadLink::m_detPacket                newest only, under m_mutex
                              │ poll(), 100 ms, GUI thread
                              ▼
                         parseDetectionPacket()                  geometry validation
                         detectionState()                        Off / Waiting / NoResponse / Empty / Boxes / Stale / Rejected
                              │ detectionsChanged()
                              ▼
                         VideoView::setDetections()              painted through pictureRect()
                         CameraPage                              status and selection text, 4 Hz
```

Detection, bounding-box metadata and tracking stay separate:

- **Detection** is the payload's detector. `TRACK_MODE` selects it.
- **Metadata** is `det_packet_t`. It is read only, and the same inspector that shows every frame shows it as
  `V2_EXTENSION`.
- **Tracking** is `MAV_CMD_USER_4`, `TRK_*` parameters and `trackingTarget()`. Its code is unchanged.

Overlay coordinates go from the packet's 1920×1080 space straight into `VideoView::pictureRect()`, the rectangle the
frame is painted in. The decoded size only matters through that rectangle, so letterboxing and resizing cannot shift
the boxes. Boxes are per-axis scaled, as the SDK example scales them.

Video and metadata are not synchronised. The packet has a frame counter of its own and no timestamp, so a box can
lag the picture by the network and decode delay.

## Stream and FOV path

The app registers no SDK stream or status callback for these. `poll()` adds every received frame to `TelemetryStore`,
which keeps the newest `VIDEO_STREAM_INFORMATION` per `stream_id` and `CAMERA_FOV_STATUS` per `camera_device_id`.
`TelemetryPage` reads the store at 4 Hz on the GUI thread.

## Gimbal settings path

`TelemetryStore` keeps the newest `PARAM_VALUE` per `param_id`, from gimbal components only. `GimbalSettingsPage` reads it
at 4 Hz. The page has no link to `GimbalPage`: settings and motion stay separate.
