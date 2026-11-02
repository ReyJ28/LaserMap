# LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
# Builds the demo scene (white/coloured shapes and text on black) as a binary PPM.
import math, sys
from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 720
img = Image.new("RGB", (W, H), (0, 0, 0))
d = ImageDraw.Draw(img)

# ring (green)
cx, cy = 260, 215
d.ellipse([cx - 150, cy - 150, cx + 150, cy + 150], fill=(0, 255, 120))
d.ellipse([cx - 92, cy - 92, cx + 92, cy + 92], fill=(0, 0, 0))

# star (yellow)
sx, sy, R, r = 640, 215, 150, 62
pts = []
for i in range(10):
    a = -math.pi / 2 + i * math.pi / 5
    rad = R if i % 2 == 0 else r
    pts.append((sx + rad * math.cos(a), sy + rad * math.sin(a)))
d.polygon(pts, fill=(255, 220, 40))

# rounded box with a hole (orange-red)
d.rounded_rectangle([860, 70, 1190, 360], radius=50, fill=(255, 90, 60))
d.ellipse([985, 160, 1065, 270], fill=(0, 0, 0))

# text
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 150)
text = "LASERMAP"
tw = d.textlength(text, font=font)
d.text(((W - tw) / 2, 435), text, font=font, fill=(60, 200, 255))

img.save(sys.argv[1] if len(sys.argv) > 1 else "scene.ppm")
