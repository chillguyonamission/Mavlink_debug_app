# Testing and troubleshooting

Build and install steps are in `README.md` and `docs/INSTALL_UBUNTU.md`.

## Hardware-free tests

`scripts/build.sh` builds and runs them, or:

```bash
cmake -S . -B build && cmake --build build -j2 && ctest --test-dir build --output-on-failure
```

| Test | Covers |
|---|---|
| `mavlink_frame`, `link_logic` | Framing, CRC, link states |
| `sdk_transport`, `payload_link` | Real SDK on loopback, connect and disconnect |
| `payload_control` | Exact outgoing frames (including every `MAV_CMD_USER_4` parameter of the calibration commands), ACK correlation, telemetry, stream and FOV requests, gimbal settings, profiles |
| `detection` | Packet validation, states, overlay pixels at hand-computed positions through resizes, selection, the SDK callback path, packet log limits, class guesses, tracker encoding |
| `main_window` | Offscreen GUI against a fake payload, including the detection workflow and data window, fresh-only stream and FOV display, per-model controls, and gimbal setting confirmation |
| `inspector`, `pcap`, `video` | Inspector, capture files, local RTSP server, RTSP running alone |
| `misb0601` | KLV decoder: BER, real ViO packets, scaling, sentinels, bad and random input |
| `metadata_model` | Valid, missing, invalid, stale, malformed, duplicate and delayed metadata; history and tag limits; export |
| `klv_stream` | A synthetic H.264 + KLV stream over loopback through the real receiver: all states, KLV-only, sender loss and recovery, layout change, missing plugin, retry then fail, queue and frame limits, 50 start/stop cycles, the source selector, panels, and failures while a fake payload stays connected |

`test_main_window` also writes screenshots when `SCREENSHOT_DIR` is set.

## Synthetic RTP/TS stream

`klv_stream` makes its own stream. To watch one in the app, send this and set **SETTINGS › RTP/TS + KLV** to address `127.0.0.1`,
port `5600`. It has video and no KLV, so the status should read *Decoding, no KLV in this stream*:

```bash
gst-launch-1.0 videotestsrc is-live=true ! video/x-raw,width=640,height=480,framerate=30/1 ! openh264enc ! h264parse config-interval=1 \
  ! mpegtsmux alignment=7 ! rtpmp2tpay ! udpsink host=127.0.0.1 port=5600 sync=false
```

This proves the processing path, not compatibility with any firmware.

## Reconnect and soak checks

`repeatedStartStopLeavesNothingBehind` runs 50 start/stop cycles against a live sender in every `klv_stream` run, and compares threads
and non-GPU descriptors. For a long run, this prints memory, descriptors, threads and counters every minute:

```bash
KLV_SOAK_SECONDS=3600 QTEST_FUNCTION_TIMEOUT=3700000 QT_QPA_PLATFORM=offscreen ./build/test_klv_stream soak
```

QtTest otherwise kills a test function after 5 minutes, so `QTEST_FUNCTION_TIMEOUT` must exceed the run. To see which side grows,
run the sender and receiver apart: `KLV_SOAK_ROLE=send`, and `receive` or `headless` (no widgets), both with the same `KLV_SOAK_PORT`.
The superseded count there is lower than in the test functions, because `QTest::qWait` sleeps between event passes: 42% of frames
under it, 9.6% in a real event loop with the sender and offscreen widgets in one process.

`SCREENSHOT_DIR=dir ./build/test_klv_stream screenshots` saves the KLV views against the synthetic stream.

## Memory and thread checks

Build outside the repository, since these builds are slow and large. This needs no new packages on Ubuntu 24.04.

```bash
cmake -S . -B /tmp/asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
      -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/asan -j3
ctest --test-dir /tmp/asan -V > asan.log; grep -E 'runtime error|Sanitizer' asan.log
```

Always search the log. UBSan reports do not fail a test, and with `halt_on_error=0` neither do AddressSanitizer reports,
and ctest hides the output of a passing test.

ThreadSanitizer uses `-fsanitize=thread` the same way. Run each test binary as `setarch -R ./test_x`: without it, this
kernel's address randomisation stops it with `unexpected memory mapping`, which is not a test result.

## Known resource issues

**GStreamer's `vah264dec` keeps one GPU buffer per video start.** It opened 345 `dmabuf` descriptors on first use and
kept one more per pipeline (GStreamer 1.24.2, Intel Meteor Lake, Mesa 25.2.8). A pipeline with no app code does the same,
and `avdec_h264` and `openh264dec` keep none, so the app does not cause it and has nothing of its own to release. The
descriptor limit here is 1,048,576, so this should not matter in use. If it does, `GST_PLUGIN_FEATURE_RANK=vah264dec:NONE`
selects another decoder.

**`avdec_h264` grows by about 20 MB per hour while decoding, so RTP/TS memory is not stable.** Measured with GStreamer 1.24.2 and
gst-libav 1.24.1, over loopback, with `gst-launch` and no app code:

| Pipeline after `tsdemux` | Growth |
|---|---|
| KLV branch only | 0.0 MB/h |
| `queue ! fakesink` | 0.0 MB/h |
| `queue ! h264parse ! fakesink` | 0.0 MB/h |
| `... ! avdec_h264 ! fakesink` | 20.2 MB/h |
| `... ! avdec_h264 ! videoconvert ! fakesink` | 19.6 MB/h |
| The same, x264 stream instead of openh264 | 21.0 MB/h |
| The same, 1280×720 | 22.5 MB/h |
| `... ! openh264dec ! videoconvert ! fakesink`, Baseline 1280×720, 5 minutes | 0.1 MB/h |

The app matches it (20.0 MB/h without widgets, 19.5 with them), and the test sender on its own is flat. Forcing one malloc arena did not
help (22.5 against 20.1 MB/h). Over the 58 minutes measured the growth stayed linear, and nothing longer has been run. It is not known
whether a real payload's stream does the same, or whether this is a leak or a cache inside libav.

`openh264dec` is not used because it was not tried on a real payload's stream, and it exited at once on an x264 stream, likely High
profile, which was not checked further. The H.264 the ViO beta capture showed was Constrained Baseline. RTSP uses whatever `decodebin`
picks, which is `vah264dec` on this laptop, and was not measured for this.

## Hardware checks

`hw_vio_commands` changes the payload's state and can move the gimbal. It is built but ctest never runs it. Run it
only when you have approved that step. See `docs/DEVELOPMENT_PROGRESS.md` for what is outstanding.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `CMakeCache.txt directory ... is different` | The repository moved after `build/` was created. Run `cmake --fresh -S . -B build`, then build again |
| New features do not appear in the app | The binary is older than the source. Run `cmake --build build -j2` and restart the app. Check the date with `ls -l build/mavlink_debug_app` |
| A compile is killed, or the machine freezes | Too many parallel jobs: `JOBS=2 scripts/build.sh` |
| Detection stays on **Waiting**, then **No response** | The payload sent no packets. Check its ACKs in Analyze › Commands. An ACK timeout is not a refusal, and the firmware may not support detection |
| Detection shows **Refused** | The payload rejected the mode or the stream. See the result column in Analyze › Commands |
| Detection shows **Stale** | Packets stopped. Check the link lamp, and whether another client changed `TRACK_MODE` |
| `invalid` count in the detection panel | Packets carry boxes that fail geometry checks. Inspect `V2_EXTENSION` in the MAVLink monitor |
| Telemetry shows **no fresh reply** for stream or FOV | The payload stopped answering, or never did. On ZIO the app does not ask for IR FOV, since it has no IR |
| Gimbal settings stay at **N of M received** | Replies were lost. Press Read from gimbal again; it fills the gaps |
| `PAYLOAD REPORTS X, MODEL SET TO Y` | The reported name does not fit the chosen model. Lynx and GHadron belong on MB1. If the name is right for your model, it is one the app has not seen: only VIO's real name is confirmed |
| The PAYLOAD lamp shows 2/3 | One of core, camera or gimbal sent no heartbeat within the timeout. Any MAVLink camera or gimbal ID counts |
| RTP/TS: **Waiting for data** | Nothing arrives. It may simply be silent: check the payload is streaming and its firmware sends this stream. Then check the address and port against its configuration, the interface (`ip route get 239.255.0.1`; choose the payload's interface if another holds the default route), and `ufw`. Watch with `sudo tcpdump -ni <iface> udp port 8554` |
| RTP/TS: **Receiving, no frames yet**, and it stays | Packets arrive but do not decode. Likely not RTP carrying MPEG-TS at all. Look at the log and the PID table in Analyze › KLV. No PAT or PMT in the table means the receiver cannot find the streams; restart the stream on the payload |
| RTP/TS: **Decoding, no KLV in this stream** | No KLV pad. The firmware may not send KLV. A PID labelled *PMT, PCR or no pad* with steady traffic may be a stream `tsdemux` did not expose |
| RTP/TS: **KLV only, no video** | The stream has no H.264 video pad. The panel still works. RTSP video alongside it is not supported |
| RTP/TS: settings rejected | **Apply** names the problem. Media must be `video` and encoding `MP2T`, which is all this pipeline can receive |
| RTP/TS: **Failed**, missing GStreamer elements | The message names them. See the package table in `docs/INSTALL_UBUNTU.md`, then press Start again |
| RTP/TS: **Error, restarting**, then **Failed** | The pipeline kept failing with no traffic, for example an address this machine cannot bind. Correct it and press Start |
| KLV: **malformed** count rises | Packets fail the checksum or framing, and none of their fields are applied. See Errors only in Analyze › KLV |
| KLV: **no data** on position or platform fields while the panel says LIVE | The stream does not carry those tags. They were absent in the ViO beta capture, possibly because the payload had no GPS input (not verified) |
| KLV: **duplicates** count rises, metadata goes stale | The sender repeats one packet with the same tag 2, so nothing new arrives. See `docs/KLV_STREAM.md` |
| KLV: **queue drops** rise | The GUI thread fell behind by more than 64 packets. The newest are kept. Payload control is not affected |
| RTSP: no picture | Unchanged by RTP/TS. Check the source selector says RTSP, then the transport: TCP under WSL2, UDP needs native Linux |
| Boxes look misaligned | Check the view source. Alignment is known only to be intended for EO only, and the panel warns otherwise |
