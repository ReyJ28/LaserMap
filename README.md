# LaserMap

Copyright (c) 2026 ReyJ28. All rights reserved. Proprietary — see [LICENSE](LICENSE).

A clean-room Resolume **FFGL** plugin project that turns video frames into laser point data
(target: Pangolin BEYOND). Written from scratch from public specifications; it contains no code
from, and was not derived from the binaries of, any other product.

## Status (v0.2)

| Part | State |
|---|---|
| Core: iso-contour tracing, simplification, signal extraction (Luma / Alpha / Edges, smoothing, colour key, key white, min size, max shapes) | done, unit-tested |
| Modes: Trace Clip, Beam Runners, Trace Bars, Sweep (erase/reveal, angle, glow, trail), Spotlight (afterglow, rim), Laser Cannon, Trace Reveal, Grid | done, unit-tested; modes are combinable |
| Trail: glow + spark particles, spark density/size/gravity/fade/colour; Laser Sparks on Sweep | done, unit-tested |
| Scanner: point spacing, corner dwell/angle, blank dwell, point budget | done, unit-tested |
| Colour: intensity, clip colour, fixed colour, fixed white | done |
| SafetyGuard: output disabled by default, brightness cap, point cap, NaN/range sanitising, static guard (toggle) | done, unit-tested |
| Beat sync (Off / 1 Beat / 2 Beats / 1 Bar) on Beam Runners, Bars, Sweep, Spotlight, Cannon, Reveal | written; assumes the host's `barPhase` is the beat position within the bar. **Unverified** |
| Video delay (0-8 frames), Show Mask, video masking for Sweep / Spotlight / Cannon / Reveal | written; **GLSL and FBO code never compiled or run** |
| FFGL plugin wrapper | compiled and linked on Windows; **only partially tested in Resolume** |
| ILDA recording: **Record** toggle writes the generated frames to `Documents/LaserMap/recording-<time>.ild` (ILDA format 5, 2D true colour); `lasermap_cli ... out.ild` exports a single frame | done, encoder unit-tested; **plugin side not run in Resolume**; import into BEYOND untested |
| Bridge: plugin streams frames over TCP to `lasermap_bridge` on the laser PC (wire format in `src/lasermap/wire.h`); Zone 1-16 and scan rate (kpps) travel with every frame | done and tested here (unit tests, loopback, independent Python sender); **plugin side not yet run in Resolume** |
| BEYOND sink inside the bridge (`--beyond`): frames -> an "SDK Image" in the chosen projection zone via Pangolin's SDK DLL (`ldbCreateZoneImage`, `ldbSendFrameToImage`) | written from the SDK document and unit-tested against a mock of the SDK; **never run against a real BEYOND or the real DLL**. Never calls enable-output or blackout. Watchdog blanks a zone if frames stop for 1 s |
| Sweep Style "Trace", Resolume Link | **not started** |

**Verified by the author (Windows, Resolume 7.28, BEYOND 5.5, one PC):** the plugin runs in Resolume, the bridge connects, and frames
reach BEYOND (the helper's test pattern and Resolume's Grid show in the zone preview). **Not yet verified:** output on a real laser,
the beat-sync, video delay, mask/reveal/cannon video masking, and the Record-to-ILDA import into BEYOND.

## Quick start (Windows, Resolume and BEYOND on one PC)

Download the **LaserMap-Windows** artifact (latest run in the Actions tab), unzip, close Resolume, right-click
`Install-LaserMap.ps1` > Run with PowerShell. Then in Resolume: LaserMap effect on a playing clip, tick **Send To BEYOND**,
pick the **Zone**, tick **Armed**, turn on **Grid**. The plugin starts the helper by itself (**Auto Start Helper**), the
helper finds `BEYONDIO.dll` in your BEYOND folder, and it closes itself 20 s after the plugin disconnects.

## Refinement and mode schedule

- **Smooth Curves / Optimise Order** (Scanner group): removes pixel wobble from outlines while keeping real corners, and draws shapes in a short path order (less blanked travel, less flicker). On the demo layer blanked travel dropped about 57%.
- **Schedule** group: switch on **Use Schedule** and type a plan into **Mode Schedule**, e.g.
  `0-8: Trace Clip; 8-16: Beam Runners + Sweep; 1:00-1:20: Grid`
  Times are seconds or m:ss, segments are separated by `;` or new lines, modes by `+`. End times are exclusive; outside every segment the laser is blank (`none` blanks on purpose). Bad parts are skipped and listed in `Documents\\LaserMap\\lasermap.log`.
  The schedule runs on its own clock that starts when you switch it on; **Restart Schedule** resets it to 0 (map it to the clip trigger or a button). **Loop Schedule** repeats after the last segment. The Use Schedule label shows the clock and current modes.

## Bridge (plugin -> laser PC)

Same shape as other Resolume-to-BEYOND tools: a plugin in Resolume and a small helper on the computer that
drives the laser.

1. On the laser PC run `lasermap_bridge` (port 7788 by default; `--ild out.ild` also records what it receives).
   Allow it through the firewall on the wired network adapter.
2. In the LaserMap effect: **Output > Bridge Address** (`host:port`, default `127.0.0.1:7788`), pick a **Zone**,
   tick **Send To Bridge**. The toggle's label shows `Bridge: connected, N fr/s`.
3. The plugin only sends what has passed **SafetyGuard**; with **Armed** off it sends empty frames (a live
   connection, no light). The helper's status line shows `CONNECTED`, frames per second and zone.

To drive BEYOND, run the 32-bit helper on the BEYOND PC with `BEYONDIO.dll` next to it (not included; it is Pangolin's) (or `--sdk-dll path`):
`lasermap_bridge --beyond [--flip-x] [--flip-y]`. Zones are BEYOND's own numbers; arming and blackout stay in BEYOND.
The frames land as an SDK Image per zone (`LaserMap.Z<n>`), which BEYOND processes with the zone's geometric correction.
To test the BEYOND side alone (no Resolume): `lasermap_bridge --beyond --test-pattern --zone 1` sends a grid and a moving circle.
To use LaserMap inside a cue (grid or timeline) instead: add an SDK Image named e.g. `LaserMap` to a BEYOND cue, then run
`lasermap_bridge --beyond --cue-image LaserMap`. The helper then only streams into that image and creates/deletes nothing.
Use **Grid** mode to line the laser up, and `--flip-y` / `--flip-x` if the picture comes out mirrored.

## Prebuilt Windows plugin

Every push builds in GitHub Actions (`.github/workflows/build.yml`); open the latest run in the repository's **Actions**
tab. Artifact **LaserMap-windows-x64** holds `LaserMap.dll` (Resolume's Extra Effects folder). Artifact
**LaserMap-bridge-win32** holds `lasermap_bridge.exe`, a **32-bit** helper, because Pangolin's `BEYONDIO.dll` is 32-bit.

## Build and test

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
./build/lasermap_cli - build/preview.svg      # built-in test pattern
./build/lasermap_cli in.ppm out.svg 0.5       # your own P6/P5 image, threshold 0.5
./build/lasermap_bridge --seconds 10          # bridge helper (statistics)
# demo video: python3 tools/demo/make_scene.py scene.ppm; ./build/lasermap_demo scene.ppm frames.bin;
#             python3 tools/demo/render_video.py frames.bin scene.ppm out.mp4
```

## Build the Resolume plugin

```bash
git clone https://github.com/resolume/ffgl ../ffgl
cmake -S . -B build -DLASERMAP_BUILD_FFGL=ON -DFFGL_SDK_DIR=../ffgl
cmake --build build --target LaserMap
```

Windows/Linux need GLEW. On Windows use vcpkg with the `x64-windows-static-md` triplet:
`vcpkg install glew:x64-windows-static-md`, then add
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md`
to the first cmake command. On macOS the target is a `.bundle`. Copy the result into
Resolume's FFGL plugin folder and add it as an effect. The plugin ID `LMAP` is a placeholder; ask
Resolume to register one before you distribute. The FFGL SDK is third-party (BSD-style licence) and is
deliberately not vendored into this repo.

Parameters are grouped in Resolume: Output, Modes, Edge Detection, Scanner, one group per mode, Trail and Colour.
Modes are independent on/off toggles and can run together (the point budget is split between them).
**Armed** is off by default and shows a live `pts/sh` readout. **Preview** draws the laser path over the video;
Grid always does, for calibration. Per-mode controls map to values as described in the Resolume UI tooltips.

## Laser safety

Lasers are hazardous and regulated. LaserMap only produces point data; keep the safety chain
(arming, blackout, exclusion zones, interlocks) in your laser software and hardware. `SafetyGuard`
is an extra layer, not a substitute.
