"""Generates the installer art (Maison style) as 24-bit BMPs into installer/art.
Usage: python make_images.py <fonts-dir> <version>"""
import math, os, sys
from PIL import Image, ImageDraw, ImageFont

fonts = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "plugin", "Fonts")
version = sys.argv[2] if len(sys.argv) > 2 else ""
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "art")
os.makedirs(out, exist_ok=True)

PAPER, RULE, INK, INK2, CAMEL, CAMEL_DEEP = (0xEC, 0xE6, 0xDA), (0xCD, 0xC4, 0xB4), (0x1C, 0x19, 0x16), (0x5F, 0x57, 0x4C), (0xB3, 0x9A, 0x6E), (0x94, 0x78, 0x4A)

def font(name, size):
    try:
        return ImageFont.truetype(os.path.join(fonts, name), size)
    except OSError:
        return ImageFont.load_default()

def bezier(p0, p1, p2, p3, n=80):
    pts = []
    for i in range(n + 1):
        t = i / n
        a = (1 - t) ** 3; b = 3 * (1 - t) ** 2 * t; c = 3 * (1 - t) * t ** 2; d = t ** 3
        pts.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return pts

def wizard(w, h):
    s = w / 164.0
    ss = 3   # supersample for smooth lines
    img = Image.new("RGB", (w * ss, h * ss), PAPER)
    d = ImageDraw.Draw(img)
    S = s * ss
    step = 20 * S
    y = step / 2
    while y < h * ss:
        x = step / 2
        while x < w * ss:
            k = 2.5 * S
            d.line([(x - k, y), (x + k, y)], fill=RULE, width=max(1, int(0.8 * S)))
            d.line([(x, y - k), (x, y + k)], fill=RULE, width=max(1, int(0.8 * S)))
            x += step
        y += step
    W, H = w * ss, h * ss
    ink = bezier((-10 * S, H * 0.83), (60 * S, H * 0.83), (70 * S, H * 0.66), (120 * S, H * 0.66)) + \
          bezier((120 * S, H * 0.66), (160 * S, H * 0.66), (170 * S, H * 0.55), (W + 10 * S, H * 0.55))
    d.line(ink, fill=INK, width=max(1, int(1.0 * S)))
    camel = bezier((-10 * S, H * 0.88), (60 * S, H * 0.88), (80 * S, H * 0.72), (130 * S, H * 0.72)) + \
            bezier((130 * S, H * 0.72), (170 * S, H * 0.72), (180 * S, H * 0.62), (W + 10 * S, H * 0.62))
    # stitched (dashed) camel line
    acc, on = 0.0, True
    for a, b in zip(camel, camel[1:]):
        seg = math.dist(a, b)
        if on:
            d.line([a, b], fill=CAMEL_DEEP, width=max(1, int(1.0 * S)))
        acc += seg
        if acc > (4 if on else 3) * S:
            acc, on = 0.0, not on
    logo = font("CormorantGaramond-SemiBoldItalic.ttf", int(40 * S))
    d.text((18 * S, 26 * S), "Atelier", font=logo, fill=CAMEL)
    mono = font("IBMPlexMono-Regular.ttf", int(7.5 * S))
    d.text((20 * S, 76 * S), "BY R4N", font=mono, fill=INK2)
    if version:
        d.text((20 * S, H - 22 * S), "V" + version, font=mono, fill=INK2)
    d.line([(W - 1, 0), (W - 1, H)], fill=RULE, width=max(1, ss))
    return img.resize((w, h), Image.LANCZOS)

def small(n):
    ss = 4
    img = Image.new("RGB", (n * ss, n * ss), (0xFB, 0xF9, 0xF4))
    d = ImageDraw.Draw(img)
    f = font("CormorantGaramond-SemiBoldItalic.ttf", int(n * ss * 0.78))
    bbox = d.textbbox((0, 0), "A", font=f)
    d.text(((n * ss - (bbox[2] - bbox[0])) / 2 - bbox[0], (n * ss - (bbox[3] - bbox[1])) / 2 - bbox[1]), "A", font=f, fill=CAMEL)
    return img.resize((n, n), Image.LANCZOS)

for w in (164, 205, 246, 328):
    wizard(w, int(round(w * 314 / 164))).save(os.path.join(out, f"wizard_{w}.bmp"))
for n in (55, 69, 83, 110):
    small(n).save(os.path.join(out, f"small_{n}.bmp"))
print("installer art written to", out)
