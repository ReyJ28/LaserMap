# LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
# Original video-mapping content for a 16:9 LED wall, plus a matching "laser layer" (bright shapes on black) that LaserMap
# can trace. Style notes only (starfield wall, flame ring, light pillars, light sheet, outline text); nothing copied.
#   make_mapping_content.py out_dir [--text LASERMAP] [--seconds 24] [--size 1920x1080]
# Writes out_dir/wall.mp4 and out_dir/laser_layer.mp4
import argparse, math, os, subprocess, sys
from multiprocessing import Pool
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--text", default="LASERMAP")
ap.add_argument("--seconds", type=float, default=24.0)
ap.add_argument("--size", default="1920x1080")
ap.add_argument("--fps", type=int, default=30)
args = ap.parse_args()
W, H = map(int, args.size.split("x"))
FPS, DUR = args.fps, args.seconds
N = int(FPS * DUR)
rng = np.random.default_rng(7)

# ---------------------------------------------------------------- geometry (computed once per worker)
G = {}

def init():
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    cx, cy = W / 2, H / 2
    G["xs"], G["ys"] = xs, ys
    G["r"] = np.hypot(xs - cx, ys - cy)
    G["th"] = np.arctan2(ys - cy, xs - cx)
    # stars
    n = int(W * H / 1500)
    G["sx"] = rng.integers(0, W - 1, n)
    G["sy"] = rng.integers(0, H - 1, n)
    G["sb"] = rng.uniform(0.25, 1.0, n).astype(np.float32)
    G["sp"] = rng.uniform(0, 6.28, n).astype(np.float32)
    G["sf"] = rng.uniform(0.5, 2.5, n).astype(np.float32)
    # text: fill mask + outline, rendered once
    fs = int(H * 0.17)
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", fs)
    m = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(m)
    tw = d.textlength(args.text, font=font)
    d.text(((W - tw) / 2, H * 0.5 - fs * 0.62), args.text, font=font, fill=255)
    G["text_fill"] = np.asarray(m, np.float32) / 255
    outline = np.asarray(m.filter(ImageFilter.MaxFilter(7)), np.float32) - np.asarray(m.filter(ImageFilter.MinFilter(7)), np.float32)
    outline = np.clip(outline / 255, 0, 1)
    G["text_edge"] = outline
    G["text_glow"] = np.asarray(Image.fromarray((outline * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(10)), np.float32) / 255
    G["text_x0"] = (W - tw) / 2
    G["text_x1"] = (W + tw) / 2
    # a sheet direction (about 35 degrees)
    a = math.radians(35)
    G["sheet_d"] = xs * math.cos(a) + ys * math.sin(a)
    G["sheet_span"] = (0.0, W * math.cos(a) + H * math.sin(a))
    # smooth low-frequency "nebula" for the wall background
    X = xs / W; Y = ys / H
    G["neb"] = (0.5 + 0.5 * np.sin(5 * X + 3 * np.sin(4 * Y))) * (0.5 + 0.5 * np.sin(4 * Y + 2 * np.cos(3 * X)))

def smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)

def rgb_blue(i):
    """Intensity (0..1+) -> deep blue / cyan / white-hot colour, shape (H,W,3) float."""
    i = np.clip(i, 0, 3)
    r = np.clip(i - 1.0, 0, 1) * 0.9 + np.clip(i, 0, 1) * 0.10
    g = np.clip(i - 0.2, 0, 1) * 0.80 + np.clip(i - 1.0, 0, 1) * 0.2
    b = np.clip(i * 1.15, 0, 1)
    return np.stack([r, g, b], -1)

# ---------------------------------------------------------------- scene timeline (seconds)
T_RING_IN = (0.0, 3.0)
T_RING_OUT = (9.0, 11.5)
T_PILLARS = (10.0, 13.5)
T_TEXT = (13.0, 17.5)
T_SHEET = (14.5, 19.0)
T_FADE = (21.0, DUR)

def scene(t):
    ring_in = smooth((t - T_RING_IN[0]) / (T_RING_IN[1] - T_RING_IN[0]))
    ring_out = 1 - smooth((t - T_RING_OUT[0]) / (T_RING_OUT[1] - T_RING_OUT[0]))
    ring_a = ring_in * ring_out
    R = H * 0.30 * (0.15 + 0.85 * ring_in) * (1 + 0.025 * math.sin(t * 2.4))
    pil = smooth((t - T_PILLARS[0]) / (T_PILLARS[1] - T_PILLARS[0]))
    pil_out = 1 - smooth((t - 19.5) / 2.0)
    txt = (t - T_TEXT[0]) / (T_TEXT[1] - T_TEXT[0])
    sheet = (t - T_SHEET[0]) / (T_SHEET[1] - T_SHEET[0])
    fade = 1 - smooth((t - T_FADE[0]) / max(T_FADE[1] - T_FADE[0], 1e-3))
    return dict(ring_a=ring_a, R=R, pil=pil, pil_out=pil_out, txt=txt, sheet=sheet, fade=fade)

def pillar_x(sc):
    # two pillars slide in from the wall edges to flank the text
    gap = (G["text_x1"] - G["text_x0"]) / 2 + W * 0.04
    xl = (W * 0.04) + (W / 2 - gap - W * 0.04) * sc["pil"]
    xr = (W * 0.96) - (W * 0.96 - (W / 2 + gap)) * sc["pil"]
    return xl, xr

def ring_field(t, sc):
    """Flame ring intensity (wall) built from angular noise that animates over time."""
    r, th, R = G["r"], G["th"], sc["R"]
    n = np.zeros_like(r)
    for k, ph, sp, amp in ((3, 0.4, 1.1, 0.50), (5, 1.7, -1.6, 0.35), (8, 2.9, 2.2, 0.25), (13, 0.2, -2.9, 0.16), (21, 4.4, 3.7, 0.09)):
        n += amp * np.sin(k * th + sp * t + ph)
    flame = np.clip(0.55 + 0.45 * n, 0, 1.2)
    core = np.exp(-((r - R) / (0.012 * H)) ** 2) * 2.2                     # bright thin core ring
    plume_len = (0.03 + 0.11 * flame) * H
    plume = np.where(r > R, np.exp(-(r - R) / plume_len), 0) * (1.35 * flame)  # flames licking outward
    inner = np.exp(-((r - R * 0.97) / (0.03 * H)) ** 2) * 0.6                # soft inside glow
    return core + plume + inner

def laser_ring(sc):
    r, R = G["r"], sc["R"]
    ring = ((r > R - 7) & (r < R + 7)).astype(np.float32)
    inner = ((r > R * 0.80 - 5) & (r < R * 0.80 + 5)).astype(np.float32)
    return np.clip(ring + inner, 0, 1)

def pillar_field(sc, wall):
    xs, ys = G["xs"], G["ys"]
    xl, xr = pillar_x(sc)
    vfade = np.clip(np.sin(np.clip(ys / H, 0, 1) * math.pi) * 1.6, 0, 1)
    a = sc["pil"] * sc["pil_out"]
    out = np.zeros_like(xs)
    for x0 in (xl, xr):
        core = (np.abs(xs - x0) < 9).astype(np.float32) if not wall else np.exp(-((xs - x0) / 10.0) ** 2) * 2.0 + np.exp(-((xs - x0) / 60.0) ** 2) * 0.5
        out += core * vfade
    return out * a

def sheet_field(sc, wall):
    d = G["sheet_d"]
    lo, hi = G["sheet_span"]
    if not (0 <= sc["sheet"] <= 1):
        return 0
    s = smooth(sc["sheet"])
    c = lo + (hi - lo) * s
    width = W * 0.16
    band = np.exp(-((d - c) / (width * 0.5)) ** 2) * 0.55
    edge = np.exp(-((np.abs(d - c) - width * 0.5) / 5.0) ** 2) * 1.4
    env = math.sin(math.pi * min(max(sc["sheet"], 0), 1))
    if not wall:
        return ((np.abs(np.abs(d - c) - width * 0.5) < 4)).astype(np.float32) * env
    return (band * 0.6 + edge) * env

def text_wipe(sc, soft=60):
    u = sc["txt"]
    if u <= 0:
        return 0
    x0, x1 = G["text_x0"] - soft, G["text_x1"] + soft
    edge = x0 + (x1 - x0) * min(u, 1.0)
    return np.clip((edge - G["xs"]) / soft, 0, 1)

def render(i):
    t = i / FPS
    sc = scene(t)
    xs, ys = G["xs"], G["ys"]
    # ------------- wall
    bg = 0.04 + 0.10 * G["neb"] * (0.5 + 0.5 * math.sin(t * 0.5))
    img = np.stack([bg * 0.20, bg * 0.45, bg * 1.0], -1) + np.array([0.0, 0.005, 0.03], np.float32)
    # stars
    stars = np.zeros((H, W), np.float32)
    tw = 0.65 + 0.35 * np.sin(G["sp"] + G["sf"] * t * 2.0)
    stars[G["sy"], G["sx"]] = G["sb"] * tw
    stars = np.maximum(stars, np.roll(stars, 1, 0) * 0.6)
    stars = np.maximum(stars, np.roll(stars, 1, 1) * 0.6)
    sa = 0.25 + 0.75 * smooth(t / 2.0)
    img += (stars * sa * 0.9)[..., None] * np.array([0.7, 0.85, 1.0], np.float32)
    # ring (flames) with a dark disc inside
    if sc["ring_a"] > 0.001:
        R = sc["R"]
        disc = np.clip((R * 0.96 - G["r"]) / 20 + 0.5, 0, 1) * 0.80 * sc["ring_a"]
        img *= (1 - disc)[..., None]
        img += rgb_blue(ring_field(t, sc) * sc["ring_a"])
    # pillars
    if sc["pil"] > 0 and sc["pil_out"] > 0:
        img += rgb_blue(pillar_field(sc, True) * 0.9) * 1.0
    # text (outline glow + thin bright line), wiped in
    w = text_wipe(sc)
    if not np.isscalar(w):
        fadeout = 1 - smooth((t - 19.5) / 2.0)
        glow = G["text_glow"] * w * fadeout
        line = G["text_edge"] * w * fadeout
        img += rgb_blue(glow * 1.1 + line * 1.8)
    # light sheet
    sh = sheet_field(sc, True)
    if not np.isscalar(sh):
        img += rgb_blue(sh)
    img = np.clip(img * sc["fade"], 0, 1)
    wall = (img * 255).astype(np.uint8)
    # ------------- laser layer: solid bright shapes on black (cyan/white), what the laser should trace
    L = np.zeros((H, W), np.float32)
    if sc["ring_a"] > 0.02:
        L = np.maximum(L, laser_ring(sc) * sc["ring_a"])
    if sc["pil"] > 0 and sc["pil_out"] > 0.02:
        L = np.maximum(L, pillar_field(sc, False))
    if not np.isscalar(w):
        L = np.maximum(L, G["text_edge"] * (w > 0.5) * (1 - smooth((t - 19.5) / 2.0)))
    if not np.isscalar(sh):
        L = np.maximum(L, sh)
    L = np.clip(L * sc["fade"], 0, 1)
    laser = np.stack([L * 0.35, L * 0.95, L * 1.0], -1)
    laser = (laser * 255).astype(np.uint8)
    return wall.tobytes(), laser.tobytes()

def encoder(path):
    return subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                             "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "17", "-preset", "medium",
                             "-movflags", "+faststart", path], stdin=subprocess.PIPE)

if __name__ == "__main__":
    os.makedirs(args.out, exist_ok=True)
    ew, el = encoder(os.path.join(args.out, "wall.mp4")), encoder(os.path.join(args.out, "laser_layer.mp4"))
    with Pool(os.cpu_count(), initializer=init) as pool:
        for i, (wb, lb) in enumerate(pool.imap(render, range(N), chunksize=2)):
            ew.stdin.write(wb); el.stdin.write(lb)
            if i % 60 == 0:
                print(f"frame {i} of {N}", flush=True)
    ew.stdin.close(); el.stdin.close(); ew.wait(); el.wait()
    print("done")
