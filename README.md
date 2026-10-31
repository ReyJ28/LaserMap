# LaserMap

Copyright (c) 2026 ReyJ28. All rights reserved. Proprietary — see [LICENSE](LICENSE).

A clean-room Resolume **FFGL** plugin project that turns video frames into laser point data
(target: Pangolin BEYOND). Written from scratch from public specifications; it contains no code
from, and was not derived from the binaries of, any other product.

## Status (v0.1 scaffold)

| Part | State |
|---|---|
| Core: luminance iso-contour tracing (marching squares) + Douglas-Peucker simplification | done, tested |
| Mode 1: **Trace Clip** → blanked jump + lit loop per shape, normalized `[-1,1]` coords, point budget | done, tested |
| **SafetyGuard**: output disabled by default, brightness cap, point cap, NaN/range sanitising, static-beam fade | done, tested |
| Desktop harness (`lasermap_cli`): image → SVG preview of the laser path | done |
| FFGL effect plugin (`plugin/ffgl`): video passthrough, Trace Clip, SafetyGuard, on-screen path preview | written; **syntax-checked against the real FFGL SDK headers only — never linked, loaded in Resolume, or run** |
| BEYOND output (`ILaserOutput` backend) | **not started** — plugin currently uses a no-op `NullOutput`; needs the BEYOND SDK/API docs |
| Other modes (sweep, spotlight, beam runners, …) | not started |

Nothing here has been tested with Resolume, BEYOND, or a real laser.

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

Parameters: **Laser Output** (off by default), **Preview** (draws the laser path over the video),
Threshold, Simplify, Max Points, Brightness (default 25%).

## Laser safety

Lasers are hazardous and regulated. LaserMap only produces point data; keep the safety chain
(arming, blackout, exclusion zones, interlocks) in your laser software and hardware. `SafetyGuard`
is an extra layer, not a substitute.
