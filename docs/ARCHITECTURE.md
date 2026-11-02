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
| SDK receive thread | `regPayloadDetectionChanged()` callback | Copies the packet into a queue of at most 64 under `m_mutex`; drops the oldest when full |
| GUI thread | `PayloadLink::poll()` every 100 ms | Takes both, parses, updates state, emits signals |
| GStreamer streaming thread | `VideoPlayer::onSample()` | Copies the newest frame; the GUI takes the latest |

No SDK or GStreamer thread touches a widget.

## Object detection path

```
payload ─V2_EXTENSION(248)─▶ SDK _handle_msg_v2_extension()      type, magic, version, num_boxes clamp
                              │ regPayloadDetectionChanged()     SDK receive thread
                              ▼
                         PayloadLink::m_detPackets               bounded queue, under m_mutex
                              │ poll(), 100 ms, GUI thread
                              ▼
                         parseDetectionPacket()                  geometry validation
                         detectionState()                        Off / Waiting / NoResponse / Empty / Boxes / Stale / Rejected
                              │ detectionsChanged()
                              ▼
                         VideoView::setDetections()              painted through pictureRect()
                         CameraPage                              status and selection text, 4 Hz
                         DetectionDataPage                       the pop-up, reads detections() and detectionLog()
```

`PayloadLink` keeps the last 1000 parsed packets in `detectionLog()` for the connection, past stale data and past
`detectionStop()`. The pop-up is created on first use and works only while visible, adding what it has not shown yet,
so closing or pausing it loses nothing within those 1000. The queue is not newest-only because two packets between polls
would otherwise leave a gap in the log.

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

## Payload profiles

A `PayloadProfile` is data: IDs, values and capability flags, one file per model, each including only its SDK header
because the headers reuse macro names with different values. That rule is also why palette lists repeat across the
files: a shared list would lose the compile-time link to each header.

- `MainWindow::setModel()` rebuilds `GimbalPage` and `CameraPage` from the profile, and tells `PayloadLink` whether the
  model has an IR camera.
- `PayloadLink` does not know models otherwise. Its one model fact is `setIrCamera()`, which stops IR, rangefinder and view
  requests to an EO-only model.
- Disabled controls say why in their tooltip.
- `profileMatchesReport()` compares the name the payload reports with the profile, ignoring case and punctuation, so a
  Lynx on MB1 raises no warning.

## Resources and limits

Measured on 2026-11-02 on this Intel Meteor Lake laptop, release build, with realistic gimbal status frames.

| What | Limit | Measured |
|---|---|---|
| Parsing and decoding each frame | | 8.6 µs |
| `TelemetryStore::add` | One frame per message and component | 0.07 µs |
| Monitor table | 100,000 frames, then the oldest go | About 100 MB when full |
| One poll with a full table: 20 or 200 new frames, scroll and repaint | | About 3 ms per 100 ms poll |
| Command history | 500 | |
| Detection log | 1000 packets | |
| Datagrams queued from SDK threads | 20,000, then dropped and counted | |

None of these is a bottleneck, so none was changed. Payload pages skip their 4 Hz refresh while hidden, including their
requests to the payload.

Connect and disconnect cycles leave threads and file descriptors unchanged (`reconnectingLeavesNothingBehind`). Video
restarts leave threads and non-GPU descriptors unchanged, and memory flat over 40 restarts
(`restartingLeavesNothingBehind`). See *Known resource issues* in `docs/TESTING.md`.

## Thread safety

ThreadSanitizer on `sdk_transport`, `payload_link`, `payload_control` and `detection` reports no race on the app's own
data. It does report these, all left alone because they work in practice and fixing them means changing SDK behaviour:

- Non-atomic SDK flags: `time_to_exit` and `writing_status`.
- Globals the SDK's receive thread writes and its command functions read, such as `CAMERA_COMPONENT_ID`. A command sent
  at the very moment the ID first changes could use the old one. The app already waits for the camera before its automatic
  requests.

Its other reports are inside Qt and D-Bus, which were not built with the checker, so it cannot see their locks. They look
like false positives, though that was not proven.

