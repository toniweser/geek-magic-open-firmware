#!/usr/bin/env python3
"""PXA: tiny full-screen pixel animations for the SmallTV-Ultra firmware.

The ESP8266 cannot afford a GIF decoder (24 KB of LZW tables), so animations
are shipped as raw paletted frames that the firmware expands on the fly.

    pxa.py aquarium                 render the bundled aquarium -> data/gif/aquarium.pxa
    pxa.py from-gif IN.gif          convert any GIF (resized to the grid) -> OUT.pxa
    pxa.py to-gif IN.pxa            render a .pxa back to GIF to check it

Format PXA1, little-endian:
    0   "PXA1"
    4   u16 width        grid width in pixels
    6   u16 height       grid height
    8   u8  scale        each grid pixel becomes scale x scale display pixels
    9   u8  fps
    10  u16 frameCount
    12  u8  bpp          4 (16 colors) or 8 (256 colors)
    13  u8  colorCount
    14  u16 reserved
    16  palette          colorCount x u16 RGB565
    ... frames           frameCount x ceil(width*height*bpp/8) bytes,
                         row-major, 4 bpp packs two pixels per byte, high nibble first
"""
import argparse
import math
import pathlib
import random
import struct

import numpy as np
from PIL import Image

MAGIC = b"PXA1"
HERE = pathlib.Path(__file__).parent


# ---------------------------------------------------------------- scenes
def aquarium_frames(n=60, frames=90):
    """Procedural aquarium scene, speeds chosen so the loop is seamless."""
    random.seed(7)
    tau = 2 * math.pi
    path = n + 12

    def wraps(k):
        return path * k / frames

    fish = [
        dict(x=5.0, y=14.0, v=wraps(1), c=(255, 140, 40), ph=0.3),
        dict(x=40.0, y=24.0, v=-wraps(1), c=(80, 200, 255), ph=2.1),
        dict(x=20.0, y=36.0, v=-wraps(2), c=(255, 90, 120), ph=4.0),
        dict(x=52.0, y=42.0, v=wraps(1), c=(240, 220, 80), ph=1.2),
    ]
    bubble_h = n - 7
    bubbles = [dict(x=random.randint(4, n - 5), y0=random.uniform(0, bubble_h), s=bubble_h * k / frames)
               for k in (1, 1, 2, 1, 2, 1)]
    weeds = [dict(x=round(n * (0.12 + i * 0.19)), h=random.randint(int(n * 0.2), int(n * 0.42)),
                  ph=random.uniform(0, 6)) for i in range(5)]

    out = []
    for f in range(frames):
        img = np.zeros((n, n, 3), dtype=np.uint8)

        def put(x, y, col):
            if 0 <= x < n and 0 <= y < n:
                img[y, x] = col

        def rect(x, y, w, h, col):
            for j in range(h):
                for i in range(w):
                    put(x + i, y + j, col)

        for y in range(n):
            k = y / n
            img[y, :] = (round(8 + 10 * k), round(60 - 30 * k), round(120 - 50 * k))
        rect(0, n - 5, n, 5, (196, 170, 110))
        rect(0, n - 6, n, 1, (160, 140, 90))

        t_sway = tau * 2 * f / frames
        for w in weeds:
            for i in range(w["h"]):
                sway = round(math.sin(t_sway + w["ph"] + i * 0.25) * (i / w["h"]) * 2.5)
                put(w["x"] + sway, n - 6 - i, (30, max(0, 150 - i), 60))

        t_bob = tau * 3 * f / frames
        for fs in fish:
            x = round((fs["x"] + fs["v"] * f) % path - 6)
            y = round(fs["y"] + math.sin(t_bob + fs["ph"]) * 1.5)
            d = 1 if fs["v"] > 0 else -1
            c = fs["c"]
            rect(x - 3, y - 1, 6, 3, c)
            rect(x - 2, y - 2, 4, 1, c)
            rect(x - 2, y + 2, 4, 1, c)
            rect(x - 4 * d - (1 if d > 0 else 0), y - 2, 2, 5, c)
            put(x + 2 * d, y - 1, (10, 10, 20))

        for b in bubbles:
            y = round((b["y0"] - b["s"] * f) % bubble_h)
            put(b["x"], y, (200, 230, 255))
            put(b["x"] + 1, y + 1, (120, 170, 220))

        out.append(Image.fromarray(img, "RGB"))
    return out


# ---------------------------------------------------------------- encoding
def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def exact_palette(frames, max_colors):
    """Use the scene's real colors when there are few enough; None otherwise."""
    colors = {}
    for fr in frames:
        for c in np.array(fr).reshape(-1, 3):
            colors.setdefault(tuple(int(v) for v in c), len(colors))
            if len(colors) > max_colors:
                return None
    pal = np.array(list(colors.keys()), dtype=np.int32)
    indexed = []
    for fr in frames:
        px = np.array(fr, dtype=np.int32).reshape(-1, 3)
        dist = ((px[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
        indexed.append(dist.argmin(axis=1).astype(np.uint8).reshape(fr.height, fr.width))
    return pal.flatten().tolist(), indexed, len(colors)


def quantize_shared(frames, colors):
    """One palette for all frames, built from a strip of sample frames."""
    exact = exact_palette(frames, colors)
    if exact is not None:
        return exact
    sample = frames[:: max(1, len(frames) // 8)]
    strip = Image.new("RGB", (sample[0].width * len(sample), sample[0].height))
    for i, fr in enumerate(sample):
        strip.paste(fr, (i * fr.width, 0))
    pal_img = strip.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE)
    raw = pal_img.getpalette()
    used = min(colors, max(int(np.array(pal_img).max()) + 1, 1))
    pal = np.array(raw[: used * 3], dtype=np.int32).reshape(used, 3)
    # Nearest-color mapping by hand: Pillow's quantize(palette=...) may pick
    # padding entries beyond the real palette, which the firmware cannot know.
    indexed = []
    for fr in frames:
        px = np.array(fr, dtype=np.int32).reshape(-1, 3)
        dist = ((px[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
        indexed.append(dist.argmin(axis=1).astype(np.uint8).reshape(fr.height, fr.width))
    return pal.flatten().tolist(), indexed, used


def encode(frames, scale, fps, bpp, out):
    w, h = frames[0].size
    if w * scale > 240 or h * scale > 240:
        raise SystemExit(f"{w}x{h} at scale {scale} exceeds 240x240")
    # Prefer the scene's exact colors (4 bpp when they fit in 16, else 8 bpp);
    # fall back to a median-cut palette only when there are more than 256.
    exact = exact_palette(frames, 256)
    if exact is not None and bpp is None:
        pal, indexed, used = exact
        bpp = 4 if used <= 16 else 8
    else:
        bpp = bpp or 8
        pal, indexed, used = quantize_shared(frames, 16 if bpp == 4 else 256)

    data = bytearray()
    data += MAGIC
    data += struct.pack("<HHBBHBBH", w, h, scale, fps, len(frames), bpp, used, 0)
    for i in range(used):
        data += struct.pack("<H", rgb565(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]))
    for arr in indexed:
        flat = arr.flatten()
        if bpp == 4:
            if len(flat) % 2:
                flat = np.append(flat, 0)
            data += bytes((flat[0::2] << 4) | flat[1::2])
        else:
            data += bytes(flat)

    out.parent.mkdir(exist_ok=True)
    out.write_bytes(data)
    per_frame = math.ceil(w * h * bpp / 8)
    print(f"{out}: {len(data)} bytes, {w}x{h} x{scale} @ {fps} fps, {len(frames)} frames, "
          f"{used} colors, {per_frame} bytes/frame")


def decode(path):
    data = path.read_bytes()
    if data[:4] != MAGIC:
        raise SystemExit("not a PXA1 file")
    w, h, scale, fps, count, bpp, used, _ = struct.unpack_from("<HHBBHBBH", data, 4)
    pos = 16
    pal = []
    for _ in range(used):
        (c,) = struct.unpack_from("<H", data, pos)
        pos += 2
        pal.append(((c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3))
    per_frame = math.ceil(w * h * bpp / 8)
    frames = []
    for _ in range(count):
        raw = np.frombuffer(data[pos: pos + per_frame], dtype=np.uint8)
        pos += per_frame
        idx = np.stack([raw >> 4, raw & 0x0F], axis=1).flatten()[: w * h] if bpp == 4 else raw[: w * h]
        rgb = np.array([pal[i] for i in idx], dtype=np.uint8).reshape(h, w, 3)
        frames.append(Image.fromarray(rgb, "RGB").resize((w * scale, h * scale), Image.NEAREST))
    return frames, fps


# ---------------------------------------------------------------- cli
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    a = sub.add_parser("aquarium")
    a.add_argument("--grid", type=int, default=60)
    a.add_argument("--frames", type=int, default=90)
    a.add_argument("--fps", type=int, default=15)
    a.add_argument("--out", type=pathlib.Path, default=HERE.parent / "data" / "gif" / "aquarium.pxa")

    g = sub.add_parser("from-gif")
    g.add_argument("input", type=pathlib.Path)
    g.add_argument("--grid", type=int, default=60, help="target grid edge length")
    g.add_argument("--scale", type=int, default=4)
    g.add_argument("--fps", type=int, default=0, help="0 = take from the GIF")
    g.add_argument("--bpp", type=int, choices=(4, 8), default=None, help="default: exact colors, 4 or 8 bpp as needed")
    g.add_argument("--out", type=pathlib.Path)

    t = sub.add_parser("to-gif")
    t.add_argument("input", type=pathlib.Path)
    t.add_argument("--out", type=pathlib.Path)

    args = ap.parse_args()

    if args.cmd == "aquarium":
        frames = aquarium_frames(args.grid, args.frames)
        encode(frames, 240 // args.grid, args.fps, None, args.out)
    elif args.cmd == "from-gif":
        im = Image.open(args.input)
        frames, fps = [], args.fps
        for i in range(getattr(im, "n_frames", 1)):
            im.seek(i)
            if not fps:
                fps = max(1, round(1000 / (im.info.get("duration") or 100)))
            frames.append(im.convert("RGB").resize((args.grid, args.grid), Image.LANCZOS))
        encode(frames, args.scale, fps, args.bpp, args.out or args.input.with_suffix(".pxa"))
    elif args.cmd == "to-gif":
        frames, fps = decode(args.input)
        out = args.out or args.input.with_suffix(".check.gif")
        frames[0].save(out, save_all=True, append_images=frames[1:], duration=round(1000 / fps), loop=0)
        print(f"{out}: {len(frames)} frames @ {fps} fps")


if __name__ == "__main__":
    main()
