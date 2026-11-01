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
| BEYOND live output (`ILaserOutput` backend), Scan Rate, Zone | **not started**: BEYOND's documented interfaces (TCP PangoScript, OSC, MIDI, HTTP server) are control-only; live frames need Pangolin's SDK (on request via sdk@pangolin.com). Scan Rate is a placeholder |
| Sweep Style "Trace", Resolume Link | **not started** |

Nothing here has been tested with BEYOND or a real laser.

## Prebuilt Windows plugin

Every push builds `LaserMap.dll` in GitHub Actions (`.github/workflows/build.yml`): open the latest run in the
repository's **Actions** tab and download the **LaserMap-windows-x64** artifact. Unzip it and copy the DLL into
Resolume's Extra Effects folder.

## Build and test

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
./build/lasermap_cli - build/preview.svg      # built-in test pattern
./build/lasermap_cli in.ppm out.svg 0.5       # your own P6/P5 image, threshold 0.5
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
