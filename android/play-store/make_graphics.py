"""Generates the Google Play icon (512x512) and feature graphic (1024x500) for SpatialEQ."""
import math
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
BG = (9, 10, 15)
ACCENT = (92, 217, 242)
CHANNELS = [(77, 179, 255), (255, 102, 115), (242, 242, 242), (140, 115, 255), (250, 158, 69)]


def font(size, bold=True):
    for path in ["/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc", "/Library/Fonts/Arial Bold.ttf"]:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def glow_dot(layer, cx, cy, r, color):
    halo = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(halo)
    d.ellipse([cx - r * 2.4, cy - r * 2.4, cx + r * 2.4, cy + r * 2.4], fill=color + (90,))
    layer.alpha_composite(halo.filter(ImageFilter.GaussianBlur(r)))
    d = ImageDraw.Draw(layer)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color + (255,))
    d.ellipse([cx - r * 0.4, cy - r * 0.4, cx + r * 0.4, cy + r * 0.4], fill=(255, 255, 255, 200))


def mark(size, cx, cy, scale, img):
    """Listener head with five sources in orbit (same mark as the launcher icon)."""
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for r, a in [(1.0, 70), (0.6, 120)]:
        rr = r * scale
        d.ellipse([cx - rr, cy - rr * 0.45 + scale * 0.25, cx + rr, cy + rr * 0.45 + scale * 0.25], outline=ACCENT + (a,), width=max(2, size // 200))
    head = scale * 0.28
    for i in range(int(head), 0, -1):  # shaded sphere
        t = i / head
        c = tuple(int(30 + (150 - 30) * (1 - t) ** 0.6) for _ in range(3))
        d.ellipse([cx - i - head * 0.25 * (1 - t), cy - i - head * 0.3 * (1 - t), cx + i - head * 0.25 * (1 - t), cy + i - head * 0.3 * (1 - t)], fill=c + (255,))
    for k, color in enumerate(CHANNELS):
        ang = math.radians([-150, -30, -90, 160, 20][k])
        glow_dot(layer, cx + math.cos(ang) * scale * 0.85, cy + math.sin(ang) * scale * 0.5 + scale * 0.05, scale * 0.09, color)
    img.alpha_composite(layer)


def icon():
    img = Image.new("RGBA", (512, 512), BG + (255,))
    mark(512, 256, 250, 210, img)
    img.convert("RGB").save(OUT / "icon-512.png")


def feature():
    w, h = 1024, 500
    img = Image.new("RGBA", (w, h), BG + (255,))
    d = ImageDraw.Draw(img)
    # Spectrum hills along the bottom.
    for row in range(14):
        y0 = h - 30 - row * 7
        pts = []
        for x in range(0, w + 8, 8):
            v = (math.sin(x * 0.012 + row * 0.6) * 0.5 + 0.5) * math.exp(-((x - 300) / 380) ** 2) * (1 - row / 16)
            v += 0.35 * (math.sin(x * 0.045 + row) * 0.5 + 0.5) * (1 - row / 14)
            pts.append((x, y0 - v * 70))
        alpha = int(150 * (1 - row / 14))
        d.line(pts, fill=(ACCENT if row % 3 else (250, 158, 69)) + (alpha,), width=2)
    mark(w, 800, 190, 150, img)
    d = ImageDraw.Draw(img)
    d.text((64, 80), "SpatialEQ", font=font(92), fill=(240, 244, 250))
    d.text((68, 192), "Equalizer & 3D spatial sound", font=font(38), fill=ACCENT)
    d.text((68, 244), "for every app on your phone", font=font(38), fill=(200, 205, 215))
    img.convert("RGB").save(OUT / "feature-graphic.png")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    icon()
    feature()
    print("wrote", OUT / "icon-512.png", OUT / "feature-graphic.png")
