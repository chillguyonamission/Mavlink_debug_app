# Portable Windows folder + zip from an existing MSVC build:  scripts\package-windows.ps1 [-Build build] [-Out dist]
# Needs: windeployqt on PATH (Qt bin), GST_ROOT = the GStreamer MSVC runtime (e.g. C:\gstreamer\1.0\msvc_x86_64).
# Network capture still needs Npcap installed by the user (its free licence does not allow bundling).
param([string]$Build = "build", [string]$Out = "dist")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
if (-not $env:GST_ROOT) { throw "Set GST_ROOT to the GStreamer MSVC runtime folder" }

$ver = (git describe --always --dirty --exclude '*').Trim()
$name = "MavlinkDebugApp-$ver-windows-x64"
$app = Join-Path $Out $name
if (Test-Path $app) { Remove-Item -Recurse -Force $app }
cmake --install $Build --prefix "$Out\stage"
Move-Item "$Out\stage\bin" $app
Remove-Item -Recurse -Force "$Out\stage"

windeployqt --release --no-translations --compiler-runtime "$app\mavlink_debug_app.exe" "$app\mavlink_capture.exe"
if ($LASTEXITCODE) { throw "windeployqt failed" }

# GStreamer: its DLLs next to the exe, only the plugins the app uses (main.cpp points GStreamer at them)
Copy-Item "$env:GST_ROOT\bin\*.dll" $app
New-Item -ItemType Directory "$app\gstreamer-1.0" | Out-Null
$plugins = "coreelements", "app", "udp", "rtsp", "rtp", "rtpmanager", "playback", "typefindfunctions",
           "videoconvertscale", "videoparsersbad", "mpegtsdemux", "libav"
foreach ($p in $plugins) { Copy-Item "$env:GST_ROOT\lib\gstreamer-1.0\gst$p.dll" "$app\gstreamer-1.0" }
Copy-Item "$env:GST_ROOT\libexec\gstreamer-1.0\gst-plugin-scanner.exe" $app

Compress-Archive -Path $app -DestinationPath "$Out\$name.zip" -Force
Write-Output "Built $Out\$name.zip"
