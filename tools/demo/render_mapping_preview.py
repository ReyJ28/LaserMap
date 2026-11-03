# LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
# Preview of the laser layer as LaserMap's Trace Clip mode would draw it, over the (dimmed) wall video, plus three
# stylised parallel beams that land on the moving head of the trace. A simulation, not footage of a real laser.
#   render_mapping_preview.py wall.mp4 traced.bin out.mp4
import struct, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H, FPS = 1280, 720, 30
wall_path, bin_path, out_path = sys.argv[1:4]
LASER_ONLY = "--laser-only" in sys.argv  # black background, no beams: just the traced lines and points
data = open(bin_path, "rb").read()
magic, MW, MH, TOTAL = struct.unpack_from("<iiii", data, 0)
assert magic == 0x4C4D4150
off = 16

def frames():
    global off
    for _ in range(TOTAL):
        off += 32 + 256                      # fixed header + discs
        (has_tex,) = struct.unpack_from("<i", data, off); off += 4
        (npts,) = struct.unpack_from("<i", data, off); off += 4
        pts = np.frombuffer(data, np.float32, npts * 6, off).reshape(npts, 6); off += npts * 24
        yield pts

dec = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-i", wall_path, "-vf", f"scale={W}:{H}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                       stdout=subprocess.PIPE)
enc = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS),
                        "-i", "-", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", "-preset", "medium", "-movflags", "+faststart", out_path],
                       stdin=subprocess.PIPE)
M = max(W, H)
beam_origin = np.array([W * 0.10, -30.0])
for i, pts in enumerate(frames()):
    raw = dec.stdout.read(W * H * 3)
    if len(raw) < W * H * 3:
        break
    wall = np.frombuffer(raw, np.uint8).reshape(H, W, 3).astype(np.float32) * (0.0 if LASER_ONLY else 0.62)
    layer = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    px = pts[:, 0] * M / 2 + W / 2 if len(pts) else np.array([])
    py = H / 2 - pts[:, 1] * M / 2 if len(pts) else np.array([])
    last = None
    for k in range(1, len(pts)):
        if pts[k, 5] > 0.5:
            continue
        c = np.clip(pts[k, 2:5], 0, 1)
        if c.max() < 0.03:
            continue
        col = tuple(int(v) for v in (np.clip(c * 1.25, 0, 1) * 255))
        d.line([(px[k - 1], py[k - 1]), (px[k], py[k])], fill=col, width=3)
        last = (px[k], py[k])
    # three parallel beams from the top-left onto the head of the trace
    if last is not None and TOTAL > 0 and not LASER_ONLY:
        tgt = np.array(last)
        v = tgt - beam_origin
        n = np.array([-v[1], v[0]]) / (np.linalg.norm(v) + 1e-6)
        bl = Image.new("RGB", (W, H), (0, 0, 0))
        bd = ImageDraw.Draw(bl)
        for o, colr in ((-14, (40, 255, 120)), (0, (60, 200, 255)), (14, (40, 255, 120))):
            a = beam_origin + n * o
            b = tgt + n * (o * 0.15)
            bd.line([tuple(a), tuple(b)], fill=colr, width=3)
        layer = Image.fromarray(np.maximum(np.asarray(layer), np.asarray(bl)))
    sharp = np.asarray(layer, np.float32)
    glow = np.asarray(layer.filter(ImageFilter.GaussianBlur(8)), np.float32) * 2.0
    core = np.asarray(layer.filter(ImageFilter.GaussianBlur(2)), np.float32) * 0.8
    img = np.clip(wall + sharp + glow + core, 0, 255).astype(np.uint8)
    enc.stdin.write(img.tobytes())
    if i % 120 == 0:
        print("frame", i, "of", TOTAL, flush=True)
enc.stdin.close(); enc.wait(); dec.wait()
print("done")
