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
| `payload_control` | Exact outgoing frames, ACK correlation, telemetry, profiles |
| `detection` | Packet validation, states, overlay pixels at hand-computed positions through resizes, selection, the SDK callback path, tracker encoding |
| `main_window` | Offscreen GUI against a fake payload, including the detection workflow |
| `inspector`, `pcap`, `video` | Inspector, capture files, local RTSP server |

`test_main_window` also writes screenshots when `SCREENSHOT_DIR` is set.

## Hardware checks

`hw_vio_commands` changes the payload's state and can move the gimbal. It is built but ctest never runs it. Run it
only when you have approved that step. See `docs/DEVELOPMENT_PROGRESS.md` for what is outstanding.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `CMakeCache.txt directory ... is different` | The repository moved after `build/` was created. Delete `build/` and run again |
| A compile is killed, or the machine freezes | Too many parallel jobs: `JOBS=2 scripts/build.sh` |
| Detection stays on **Waiting**, then **No response** | The payload sent no packets. Check its ACKs in Analyze › Commands. An ACK timeout is not a refusal, and the firmware may not support detection |
| Detection shows **Refused** | The payload rejected the mode or the stream. See the result column in Analyze › Commands |
| Detection shows **Stale** | Packets stopped. Check the link lamp, and whether another client changed `TRACK_MODE` |
| `invalid` count in the detection panel | Packets carry boxes that fail geometry checks. Inspect `V2_EXTENSION` in the MAVLink monitor |
| Boxes look misaligned | Check the view source. Alignment is known only to be intended for EO only, and the panel warns otherwise |
