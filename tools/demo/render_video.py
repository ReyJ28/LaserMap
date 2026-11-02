# LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
# Renders the frames dumped by lasermap_demo into a video: dimmed "projection" (masked like the plugin masks the
# picture) plus the laser path with glow. A simulation of the laser preview, not a photo of a real laser.
#   render_video.py frames.bin scene.ppm out.mp4            (full video)
#   render_video.py frames.bin scene.ppm stills_dir --stills (one PNG from the middle of every mode)
import struct, subprocess, sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H, FPS = 1280, 720, 30
MODES = [
    ("TRACE CLIP", "Outlines every bright shape as laser points", ["", ""]),
    ("BEAM RUNNERS", "Hot beams race along the outlines with fading tails", ["FORWARD", "ALTERNATING"]),
    ("TRACE BARS", "Outlines are drawn only inside scrolling bands", ["LEFT TO RIGHT", "GRID (BOTH)"]),
    ("SWEEP", "A beam erases the picture, then draws it back in", ["ERASE", "REVEAL"]),
    ("SPOTLIGHT", "A wandering laser circle; the picture exists only inside it", ["", ""]),
    ("LASER CANNON", "Bolts blast the covering off hit by hit, then the rest floods away", ["", ""]),
    ("TRACE REVEAL", "Each shape is traced in turn and revealed when its outline closes", ["", ""]),
    ("GRID", "Calibration pattern for lining up laser and picture", ["", ""]),
]
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
REG = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
f_title, f_sub, f_small, f_tag = (ImageFont.truetype(BOLD, 44), ImageFont.truetype(REG, 24),
                                  ImageFont.truetype(REG, 17), ImageFont.truetype(BOLD, 24))

def read_ppm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split()); f.readline()
        return np.frombuffer(f.read(), np.uint8).reshape(h, w, 3)

scene = read_ppm(sys.argv[2]).astype(np.float32)
assert scene.shape[:2] == (H, W)
M = max(W, H)
ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
LX = (xs + 0.5 - W / 2) * 2 / M
LY = -(ys + 0.5 - H / 2) * 2 / M

data = open(sys.argv[1], "rb").read()
magic, MW, MH, TOTAL = struct.unpack_from("<iiii", data, 0)
assert magic == 0x4C4D4150
off = 16

def frames():
    global off
    for _ in range(TOTAL):
        mode, kind, nx, ny, edge, keepg, gain, nd = struct.unpack_from("<iifffifi", data, off); off += 32
        discs = np.array(struct.unpack_from("<64f", data, off), np.float32).reshape(16, 4); off += 256
        (has_tex,) = struct.unpack_from("<i", data, off); off += 4
        tex = None
        if has_tex:
            tex = np.frombuffer(data, np.uint8, MW * MH, off).reshape(MH, MW); off += MW * MH
        (npts,) = struct.unpack_from("<i", data, off); off += 4
        pts = np.frombuffer(data, np.float32, npts * 6, off).reshape(npts, 6); off += npts * 24
        yield mode, kind, nx, ny, edge, keepg, gain, nd, discs, tex, pts

def visibility(kind, nx, ny, edge, keepg, gain, nd, discs, tex):
    if kind == 1:
        d = LX * nx + LY * ny
        return ((d >= edge) if keepg else (d <= edge)).astype(np.float32)
    if kind == 2:
        v = np.zeros((H, W), np.float32)
        for k in range(nd):
            cx, cy, r, a = discs[k]
            v = np.maximum(v, a * (((LX - cx) ** 2 + (LY - cy) ** 2) < r * r))
        return np.maximum(v, gain)
    if kind == 3 and tex is not None:
        t = np.asarray(Image.fromarray(tex).resize((W, H), Image.BILINEAR), np.float32) / 255
        return np.maximum(t, gain)
    return np.ones((H, W), np.float32)

def draw_frame(i, mode, kind, nx, ny, edge, keepg, gain, nd, discs, tex, pts):
    vis = visibility(kind, nx, ny, edge, keepg, gain, nd, discs, tex)
    base = scene * (0.42 * vis)[..., None]
    layer = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    px = pts[:, 0] * M / 2 + W / 2
    py = H / 2 - pts[:, 1] * M / 2
    for k in range(1, len(pts)):
        if pts[k, 5] > 0.5:
            continue
        c = np.clip(pts[k, 2:5], 0, 1)
        if c.max() < 0.02:
            continue
        col = tuple(int(v) for v in (c * 255))
        d.line([(px[k - 1], py[k - 1]), (px[k], py[k])], fill=col, width=3)
    sharp = np.asarray(layer, np.float32)
    glow = np.asarray(layer.filter(ImageFilter.GaussianBlur(9)), np.float32) * 2.2
    core = np.asarray(layer.filter(ImageFilter.GaussianBlur(2)), np.float32) * 0.8
    img = np.clip(base + sharp + glow + core, 0, 255).astype(np.uint8)
    im = Image.fromarray(img)
    t = ImageDraw.Draw(im, "RGBA")
    local = (i % (10 * FPS)) / FPS
    name, desc, tags = MODES[mode]
    tag = tags[0] if local < 5 else tags[1]
    t.rectangle([0, 0, W, 108], fill=(0, 0, 0, 150))
    t.text((28, 14), f"MODE {mode + 1}/8", font=f_small, fill=(160, 170, 180, 255))
    t.text((28, 36), name, font=f_title, fill=(255, 255, 255, 255))
    if tag:
        tw = t.textlength(tag, font=f_tag)
        t.rounded_rectangle([W - 28 - tw - 24, 30, W - 28, 74], radius=10, fill=(40, 200, 140, 220))
        t.text((W - 28 - tw - 12, 38), tag, font=f_tag, fill=(0, 0, 0, 255))
    t.text((330, 52), desc, font=f_sub, fill=(200, 210, 220, 255)) if False else None
    t.rectangle([0, H - 62, W, H], fill=(0, 0, 0, 170))
    t.text((28, H - 56), desc, font=f_sub, fill=(220, 228, 236, 255))
    seg_w = (W - 56) / 8
    for m in range(8):
        x0 = 28 + m * seg_w
        t.rectangle([x0 + 2, H - 20, x0 + seg_w - 2, H - 14], fill=(70, 78, 88, 255))
        if m < mode:
            t.rectangle([x0 + 2, H - 20, x0 + seg_w - 2, H - 14], fill=(40, 200, 140, 255))
        elif m == mode:
            t.rectangle([x0 + 2, H - 20, x0 + 2 + (seg_w - 4) * local / 10, H - 14], fill=(40, 200, 140, 255))
    t.text((W - 28 - 560, H - 52 + 28), "LaserMap  |  preview simulation of the laser path, not a photo of a real laser",
           font=f_small, fill=(130, 140, 150, 255)) if False else None
    msg = "LaserMap preview simulation, not a real laser"
    t.text((W - 28 - t.textlength(msg, font=f_small), H - 54), msg, font=f_small, fill=(150, 160, 170, 255))
    return im

mode_arg = sys.argv[3]
stills = len(sys.argv) > 4 and sys.argv[4] == "--stills"
if stills:
    os.makedirs(mode_arg, exist_ok=True)
    want = {m * 300 + 120 for m in range(8)} | {3 * 300 + 220}
    for i, fr in enumerate(frames()):
        if i in want:
            draw_frame(i, *fr).save(f"{mode_arg}/mode{fr[0] + 1}_{i}.png")
else:
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                           "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18",
                           "-preset", "medium", "-movflags", "+faststart", mode_arg], stdin=subprocess.PIPE)
    for i, fr in enumerate(frames()):
        ff.stdin.write(draw_frame(i, *fr).tobytes())
        if i % 300 == 0:
            print("frame", i, "of", TOTAL, flush=True)
    ff.stdin.close(); ff.wait()
