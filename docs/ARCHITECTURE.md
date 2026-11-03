# Architecture

The module list is in `README.md` under *Layout*. This page covers how data moves and who owns what.

## Ownership

- `PayloadLink` is the only owner of `PayloadSdkInterface`, and `payload_link.cpp` is the only file that includes the
  SDK's header. All commands use the SDK's own UDP transport; the app opens no second command socket.
- `pcap_capture` is a separate observer. Its data never affects link health or command tracking.
- `VideoPlayer` is a separate RTSP pipeline, independent of the MAVLink link.
- `KlvReceiver` is a separate RTP/MPEG-TS pipeline. It opens its own UDP socket for the stream and knows nothing of the SDK,
  `PayloadLink` or MAVLink. They share no lock, timer or queue.
- `VideoPage` owns `VideoPlayer`, `KlvReceiver` and `MetadataModel`, and starts only the selected source.

## Threads

| Thread | Runs | Rule |
|---|---|---|
| SDK I/O threads | `regTransportObserver()` callback | Copies the datagram into a bounded queue under `m_mutex` |
| SDK receive thread | `regPayloadDetectionChanged()` callback | Copies the packet into a queue of at most 64 under `m_mutex`; drops the oldest when full |
| GUI thread | `PayloadLink::poll()` every 100 ms | Takes both, parses, updates state, emits signals |
| GStreamer streaming thread | `VideoPlayer::onSample()` | Copies the newest frame; the GUI takes the latest |
| GStreamer streaming threads | `KlvReceiver` probes and appsink callbacks | Count; copy the newest frame; copy KLV into a queue of at most 64, dropping the oldest |
| GUI thread | `KlvReceiver::drainKlv()`, `MetadataModel` | Takes the KLV queue, parses it, and updates the model. Model and widgets are touched only here |

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

## RTP/TS and KLV path

```
udpsrc ─▶ rtpmp2tdepay ─▶ tsdemux ─ pad-added, by caps
   │ count        │ count per PID  ├ video/x-h264 ─▶ queue (leaky) ─▶ h264parse ─▶ avdec_h264 ─▶ videoconvert ─▶ appsink (1, drop)
                                   │                                                                        │ newest QImage, under m_mutex
                                   │                                                                        ▼ frameReady(), coalesced
                                   │                                                                   VideoView::setImage()
                                   ├ meta/x-klv ──▶ queue (leaky, 64) ─▶ appsink (64, drop)
                                   │                                     │ enqueueKlv(): 64 packets of at most 64 KiB, oldest dropped and counted
                                   │                                     ▼ drainKlv() on the GUI thread, klvReceived()
                                   │                                MetadataModel::ingestNow()  ─ 10 Hz changed() ─▶ KlvPanel
                                   │                                                                4 Hz, read ─────▶ KlvPage
                                   └ anything else ─▶ fakesink (listed in Analyze › KLV)
```

Parsing runs on the GUI thread because a packet is small. The size and queue limits are what keep it cheap: a batch cannot exceed
64 packets of 64 KiB each.

**States.** `updateState()` works out Waiting, Receiving, Decoding, NoKlvPad and KlvOnly from the last second's counters and the
pads present. Error is set on a bus error or a failed start, and Failed when the pipeline cannot run.

**Restarts.** A bus error schedules one restart after 2 s. Five in a row with no UDP traffic gives up as Failed, and only the next
Start retries. `stop()` cancels a pending restart, then joins the streaming threads with `GST_STATE_NULL`, and only then clears the
state the callbacks use. Work a stopped pipeline left queued carries an old generation number and is ignored.

**Layout changes.** When `tsdemux` removes a pad, the whole pipeline is rebuilt. Rerouting a live demuxer is not safe here: a
replacement pad can arrive before the old one leaves, with the same name.

**Not synchronised.** The model records local arrival time, and KLV buffers are handed over without their PTS. Nothing relates a
packet to a frame, so the panel says *latest stream metadata* and does not imply otherwise.

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
| RTP/TS frames kept | 1, the newest; replaced ones counted as superseded | |
| KLV queue to the GUI thread | 64 packets of 64 KiB, then dropped and counted | |
| KLV packet history | 30, plus 30 bad ones | |
| KLV tags in the table | 256, then dropped and counted | |
| RTP/TS log | 100 lines | |

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

