#!/usr/bin/env python3
"""Генерирует иконки SHELTER (PNG/ICO/ICNS) из того же контура «S», что и логотип в вёрстке."""
import math, os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 2048  # рабочий размер


def s_path():
    pts = []
    def line(a, b, n=40):
        for i in range(n + 1):
            t = i / n
            pts.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    def arc(c, r, a0, a1, n=80):
        for i in range(n + 1):
            a = math.radians(a0 + (a1 - a0) * i / n)
            pts.append((c[0] + r * math.cos(a), c[1] + r * math.sin(a)))
    line((16.5, 5.5), (10, 5.5))
    arc((10, 8.75), 3.25, -90, -270)   # левая дуга (против часовой)
    line((10, 12), (14, 12))
    arc((14, 15.25), 3.25, -90, 90)    # правая дуга
    line((14, 18.5), (7.5, 18.5))
    return pts


def render():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # фон: скруглённый квадрат с градиентом
    bg = Image.new("RGBA", (S, S))
    px = bg.load()
    for y in range(S):
        for x in range(S):
            t = (x + y) / (2 * S)
            px[x, y] = (int(20 + 26 * t), int(18 + 14 * t), int(40 + 70 * t), 255)
    m = Image.new("L", (S, S), 0)
    pad = int(S * 0.055)
    ImageDraw.Draw(m).rounded_rectangle((pad, pad, S - pad, S - pad), radius=int(S * 0.225), fill=255)
    img.paste(bg, (0, 0), m)
    # «S»
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    k = S * 0.56 / 13.0                       # 13 ед. высоты -> 56 % иконки
    cx, cy = 12.375, 12.0
    w = 2.0 * k
    def P(p):
        return (S / 2 + (p[0] - cx) * k, S / 2 + (p[1] - cy) * k)
    pts = [P(p) for p in s_path()]
    for p in pts:
        d.ellipse((p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2), fill=(141, 140, 255, 255))
    glow = layer.filter(ImageFilter.GaussianBlur(S * 0.02))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, layer)
    return img


def main():
    big = render()
    def sz(n):
        return big.resize((n, n), Image.LANCZOS)
    os.makedirs(f"{ROOT}/resources/ui", exist_ok=True)
    os.makedirs(f"{ROOT}/win", exist_ok=True)
    os.makedirs(f"{ROOT}/mac", exist_ok=True)
    sz(256).save(f"{ROOT}/resources/ui/icon-256.png")
    sz(256).save(f"{ROOT}/win/shelter.ico",
                 sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    sz(1024).save(f"{ROOT}/mac/Shelter.icns")
    print("ok")


main()
