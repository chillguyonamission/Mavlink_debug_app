# RTP/TS stream and KLV metadata

The **RTP/TS video + KLV** source plays H.264 from an RTP/MPEG-TS stream, and decodes MISB ST 0601 KLV from the same stream. It runs
beside the MAVLink link and shares nothing with it: a stream failure does not touch payload control.

## What has been verified

| Claim | Evidence |
|---|---|
| The pipeline receives H.264 and KLV from one TS | Synthetic stream over loopback (`klv_stream`) |
| A real payload sends this stream | **One capture, by the KLV monitor project:** ViO beta firmware, 2026-10-29. **Not run from this app.** No payload was reachable during the integration |
| Release firmware sends it | **Unknown** |
| A payload sends RTSP and RTP/TS at the same time | **Unknown.** That is why KLV runs only as this source, not alongside RTSP |

## Settings

In **SETTINGS › RTP/TS + KLV**. **Apply** checks them, saves them, and restarts the stream if it is running. They are stored with
QSettings in the `klv` group, separate from the connection profiles. Saved settings that no longer validate, such as an interface
that was unplugged, are logged and replaced by the defaults.

| Setting | Default | Accepted |
|---|---|---|
| Address | `239.255.0.1` | An IPv4 address: a multicast group to join, or a local address to bind |
| Port | `8554` | 1 to 65535 |
| Interface | Kernel chooses | Any interface on this machine. It decides where a multicast join goes |
| RTP media | `video` | Only `video` |
| RTP encoding | `MP2T` | Only `MP2T` |
| RTP clock rate | `90000` | Above 0 |

**The defaults come from the KLV monitor, as seen on ViO beta firmware. They are not universal payload settings.** Media and encoding
accept one value each because that is all this pipeline can receive. Anything else would never negotiate, and would only look like
silence.

The values are set as element properties and caps fields, never parsed as pipeline syntax. Bare KLV over UDP, without MPEG-TS, is not
supported. Neither is hardware decoding: this path always uses `avdec_h264`.

## Status

| Status | Meaning |
|---|---|
| Stopped | Not running |
| Waiting for data | Listening, no UDP traffic. A silent multicast group looks exactly like this, so it is not an error |
| Receiving, no frames yet | UDP traffic, nothing decoded |
| Decoding | Frames, and the stream has a KLV pad |
| Decoding, no KLV in this stream | Frames, no KLV pad |
| KLV only, no video in this stream | A KLV pad and no video |
| Error, restarting | A pipeline error. It restarts after 2 s |
| Failed | A missing plugin, or five restarts in a row with no traffic. Press Start to retry |

## Supported KLV tags

A packet must have the UAS Datalink LS key, valid framing, and a correct checksum (tag 1). Otherwise it counts as malformed and **none
of its fields are applied**.

| Tag | Name | Unit | Encoding |
|---|---|---|---|
| 1 | Checksum | | uint16, verified |
| 2 | Precision Time Stamp | UTC | uint64 µs since 1970 |
| 3 | Mission ID | | text |
| 4 | Platform Tail Number | | text |
| 5 | Platform Heading | ° | uint16, 0 to 360 |
| 6 | Platform Pitch | ° | int16, ±20 |
| 7 | Platform Roll | ° | int16, ±50 |
| 10 | Platform Designation | | text |
| 11 | Image Source Sensor | | text |
| 12 | Image Coordinate System | | text |
| 13, 14 | Sensor Latitude, Longitude | ° | int32, ±90, ±180 |
| 15 | Sensor True Altitude | m | uint16, −900 to 19000 |
| 16, 17 | Sensor Horizontal, Vertical FOV | ° | uint16, 0 to 180 |
| 18 | Sensor Relative Azimuth | ° | uint32, 0 to 360 |
| 19 | Sensor Relative Elevation | ° | int32, ±180. Gremsy's page lists uint32; a real ViO value only makes sense as int32 |
| 20 | Sensor Relative Roll | ° | uint32, 0 to 360 |
| 21 | Slant Range | m | uint32, 0 to 5,000,000 |
| 22 | Target Width | m | uint16, 0 to 10000 |
| 23, 24 | Frame Center Latitude, Longitude | ° | int32, ±90, ±180 |
| 25 | Frame Center Elevation | m | uint16, −900 to 19000 |
| 57 | Ground Range | m | uint32, 0 to 5,000,000. ST 0601 definition, never seen from a ViO |
| 58 | Platform Fuel Remaining | kg | uint16, 0 to 10000. ST 0601 definition, never seen from a ViO |
| 65 | UAS Datalink LS Version | | uint8 |

Any other tag appears in Analyze › KLV as `Unknown tag N` with its raw bytes. At most 256 tags are tracked, and further ones are
dropped and counted.

**The one real capture carried only tags 1, 2, 16–21 and 65.** Expect `no data` on position, platform and frame-center fields until
that changes. The payload reported no GPS then, which may be why, but that is not verified.

## How values are judged

| What you see | Meaning |
|---|---|
| The value | Decoded from the newest packet carrying that tag, and packets are still arriving |
| `no data` | Never received. Never a zero |
| `invalid` | The last value had the wrong length or was the ST 0601 out-of-range marker. The old value is not shown as current. Analyze › KLV shows it marked *last valid* |
| `value (stale N s)` | No fresh packet for over 2 s. The last value and its age |

- **Freshness is per stream**, from the local arrival time. It is not per tag, because tags can be sent only on change. So a tag that
  stops arriving while others continue still reads as valid. Compare *Updated* in Analyze › KLV.
- **Tag 2 is the payload's own clock.** On the captured ViO it was over two months off the real date, so do not read it as wall time.
- **Duplicates:** a packet with a tag 2 that is byte-identical to the previous one is counted, and does not refresh freshness. Packets
  with no tag 2 cannot be judged this way and are treated as new.
- **Delayed packets:** tag 2 older than the newest seen, by less than 5 s, is counted and not applied, so it cannot replace newer values.
  A larger jump back means the sender's clock was reset, so it is accepted.

## Not supported

- **Platform position and target coordinates.** The parser has no such tags, and none are inferred.
- **Frame-level synchronisation.** No frame ID or PTS mapping exists. KLV in the panel is the latest stream metadata, and does not describe a
  particular frame. Claiming otherwise would need a common timestamp mapping and a measured drift limit.
- **A map.** See above, and the station has no map framework.
- **KLV alongside RTSP video.** Unverified topology, so not built.
