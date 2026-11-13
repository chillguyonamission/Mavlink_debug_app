# Windows (portable)

1. Download `MavlinkDebugApp-<version>-windows-x64.zip` from the latest green CI run (GitHub → Actions → build →
   Artifacts → `windows-portable`). Unzip it anywhere: a local folder, a USB stick or a synced cloud folder.
2. Run `mavlink_debug_app.exe`. Windows SmartScreen may warn about an unsigned app: **More info → Run anyway**.
3. Connect the payload. A VIO over USB-C appears as a USB network adapter and should get `192.168.55.100` (or `.3`).
   If it does not, set the adapter to a static `192.168.55.100/24`.
4. **DIAGNOSE → Health check → Run network check** confirms the PC adapter, the web app and video ports.

Notes:
- Nothing is installed or written next to the app. Settings live in the registry (`HKCU\Software\Gremsy\MavlinkDebugApp`),
  GStreamer's plugin cache in `%LOCALAPPDATA%\Gremsy\MavlinkDebugApp\cache`.
- **Network capture** (inspector) needs [Npcap](https://npcap.com) installed on that PC. Its free licence does not allow
  shipping it inside the zip. Without it the capture tools say so; everything else works.
- **Firewall:** the first connection may raise a Windows Firewall prompt. Allow it on private networks, or RTP/KLV
  video (UDP) will not arrive.
- **KLV multicast interface:** adapters are listed by their system names (e.g. `ethernet_32769`). Leave it empty unless the
  PC has several networks.
- UART control is not available on Windows (the SDK's serial code is Linux-only); the app uses UDP anyway.
