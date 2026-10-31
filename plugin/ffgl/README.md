# FFGL wrapper (planned)

Copyright (c) 2026 ReyJ28. All rights reserved.

Plan:

1. Vendor/fetch the open-source FFGL SDK (github.com/resolume/ffgl) and follow its sample plugin
   for the 2.1 entry points, parameter registration, and build setup on Windows/macOS.
2. Each frame: read back the input texture to a `lasermap::Frame` (downscale first, e.g. ≤ 320 px
   wide, to keep `traceClip` cheap), call `traceClip`, pass the result through `SafetyGuard`.
3. Parameters: threshold, simplify, max points, brightness, output enable (default off).
4. Hand the `PointFrame` to a BEYOND output backend (separate module, off the render thread).
   Interface to be confirmed from Pangolin's published SDK/API docs.
5. Never touch BEYOND arming/blackout; on any error output nothing (video must keep playing).

Not implemented yet; no FFGL code is committed because it can't be built or verified in the
current environment.
