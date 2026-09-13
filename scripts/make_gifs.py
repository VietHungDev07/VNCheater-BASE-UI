"""Turn still launcher backgrounds into looping animated GIFs."""
from __future__ import annotations

import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance

ROOT = Path(__file__).resolve().parents[1] / "assets" / "images"
FRAMES = 20
DURATION_MS = 90


def ken_burns(im: Image.Image, t: float, zoom_a=1.05, zoom_b=1.12, pan_x=14, pan_y=8) -> Image.Image:
    w, h = im.size
    z = zoom_a + (zoom_b - zoom_a) * (0.5 - 0.5 * math.cos(2 * math.pi * t))
    cw, ch = max(2, int(w / z)), max(2, int(h / z))
    ox = int((w - cw) / 2 + math.sin(2 * math.pi * t) * pan_x)
    oy = int((h - ch) / 2 + (0.5 - 0.5 * math.cos(2 * math.pi * t)) * pan_y)
    ox = max(0, min(ox, w - cw))
    oy = max(0, min(oy, h - ch))
    return im.crop((ox, oy, ox + cw, oy + ch)).resize((w, h), Image.Resampling.LANCZOS)


def overlay_rain(frame: Image.Image, t: float, rng: random.Random, density=86) -> Image.Image:
    base = frame.convert("RGBA")
    rain = Image.new("RGBA", base.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(rain)
    w, h = base.size
    for i in range(density):
        speed = 0.55 + (i % 7) * 0.08
        x = (rng.random() * (w + 80) + t * (140 + i % 5 * 30)) % (w + 80) - 40
        y = (rng.random() * (h + 90) + t * (380 + i % 4 * 70) * speed) % (h + 90) - 40
        length = 10 + (i % 9) * 2.4
        alpha = 36 + (i % 5) * 8
        draw.line((x, y, x + 2.5, y + length), fill=(214, 236, 255, alpha), width=1)
    spark = Image.new("RGBA", base.size, (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(spark)
    for i in range(18):
        x = (rng.random() * w + math.sin(t * 6.28 + i) * 12) % w
        y = (rng.random() * h + math.cos(t * 6.28 + i * 0.7) * 10) % h
        sdraw.ellipse((x, y, x + 2, y + 2), fill=(230, 248, 255, 50 + i % 20))
    return Image.alpha_composite(Image.alpha_composite(base, rain), spark)


def overlay_sun(frame: Image.Image, t: float, rng: random.Random) -> Image.Image:
    base = frame.convert("RGBA")
    dust = Image.new("RGBA", base.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(dust)
    w, h = base.size
    for i in range(28):
        x = (rng.random() * w + math.sin(t * 6.28 + i * 0.9) * (18 + i % 8)) % w
        y = (rng.random() * h + math.cos(t * 6.28 + i * 1.1) * (10 + i % 6) - t * 12) % h
        r = 1 + i % 3
        draw.ellipse((x, y, x + r, y + r), fill=(255, 250, 230, 40 + i % 30))
    glow = Image.new("RGBA", base.size, (0, 0, 0, 0))
    pulse = 8 + int(10 * (0.5 - 0.5 * math.cos(2 * math.pi * t)))
    ImageDraw.Draw(glow).rectangle((0, 0, w, int(h * 0.42)), fill=(255, 248, 230, pulse))
    return Image.alpha_composite(Image.alpha_composite(base, glow), dust)


def grade(frame: Image.Image, t: float, amount=0.035) -> Image.Image:
    factor = 1 + amount * math.sin(2 * math.pi * t)
    return ImageEnhance.Brightness(frame.convert("RGB")).enhance(factor)


def build_frames(path: Path, kind: str) -> list[Image.Image]:
    src = Image.open(path).convert("RGB")
    frames = []
    for i in range(FRAMES):
        t = i / FRAMES
        rng = random.Random(1000 + i * 17)
        shot = ken_burns(src, t)
        if kind == "rain":
            shot = overlay_rain(shot, t, rng)
        else:
            shot = overlay_sun(shot, t, rng)
        frames.append(grade(shot, t))
    return frames


def save_gif(frames: list[Image.Image], dest: Path) -> None:
    palette_src = frames[0].quantize(colors=192, method=Image.Quantize.MEDIANCUT)
    quantized = [im.convert("RGB").quantize(palette=palette_src, dither=Image.Dither.FLOYDSTEINBERG) for im in frames]
    dest.parent.mkdir(parents=True, exist_ok=True)
    quantized[0].save(
        dest,
        save_all=True,
        append_images=quantized[1:],
        duration=DURATION_MS,
        loop=0,
        optimize=True,
        disposal=2,
    )


def main() -> None:
    jobs = (
        ("intro.png", "intro.gif", "rain"),
        ("updater.png", "updater.gif", "sun"),
        ("auth.png", "auth.gif", "rain"),
        ("launcher.png", "launcher.gif", "sun"),
    )
    for src_name, dest_name, kind in jobs:
        src = ROOT / src_name
        dest = ROOT / dest_name
        print(f"Animating {src_name} -> {dest_name} ({kind})")
        frames = build_frames(src, kind)
        save_gif(frames, dest)
        print(f"  {dest.stat().st_size / 1024 / 1024:.2f} MB")


if __name__ == "__main__":
    main()
