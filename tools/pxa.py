#!/usr/bin/env python3
"""PXA: tiny full-screen pixel animations for the SmallTV-Ultra firmware.

The ESP8266 cannot afford a GIF decoder (24 KB of LZW tables), so animations
are shipped as raw paletted frames that the firmware expands on the fly.

    pxa.py scenes                   render the four daytime scenes -> data/gif/{morning,day,evening,night}.pxa
    pxa.py from-gif IN.gif          convert any GIF (resized to the grid) -> OUT.pxa
    pxa.py to-gif IN.pxa            render a .pxa back to GIF to check it
    pxa.py readme-gifs              render a short GIF per scene -> docs/{morning,day,evening,night}.gif

Format PXA2, little-endian (PXA1 is the same without run-length encoding):
    0   "PXA2"
    4   u16 width        grid width in pixels
    6   u16 height       grid height
    8   u8  scale        each grid pixel becomes scale x scale display pixels
    9   u8  fps
    10  u16 frameCount
    12  u8  bpp          4 (16 colors) or 8 (256 colors); run-length frames are always 8
    13  u8  colorCount
    14  u8  flags        bit 0: frames are run-length encoded
    15  u8  reserved
    16  palette          colorCount x u16 RGB565
    ... frames           raw: frameCount x ceil(width*height*bpp/8) bytes, row-major,
                              4 bpp packs two pixels per byte, high nibble first
                         rle: per frame a sequence of (u8 length-1, u8 index) runs
                              until width*height pixels are covered
"""
import argparse
import math
import pathlib
import random
import struct

import numpy as np
from PIL import Image

MAGIC_RAW = b"PXA1"
MAGIC = b"PXA2"
FLAG_RLE = 0x01
HERE = pathlib.Path(__file__).parent
DATA_GIF = HERE.parent / "data" / "gif"
TAU = 2 * math.pi

N = 60          # grid edge
PATH = N + 12   # fish travel distance per wrap (off-screen on both sides)


# ---------------------------------------------------------------- drawing helpers
class Canvas:
    def __init__(self, n):
        self.n = n
        self.img = np.zeros((n, n, 3), dtype=np.int32)

    def put(self, x, y, c):
        if 0 <= x < self.n and 0 <= y < self.n:
            self.img[y, x] = c

    def get(self, x, y):
        return tuple(int(v) for v in self.img[y, x])

    def rect(self, x, y, w, h, c):
        x0, y0 = max(x, 0), max(y, 0)
        x1, y1 = min(x + w, self.n), min(y + h, self.n)
        if x1 > x0 and y1 > y0:
            self.img[y0:y1, x0:x1] = c

    def row(self, y, c):
        self.img[y, :] = c

    def blend(self, x, y, c, k):
        if 0 <= x < self.n and 0 <= y < self.n:
            self.img[y, x] = mix(self.img[y, x], c, k)

    def image(self):
        return Image.fromarray(self.img.astype(np.uint8), "RGB")


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def draw_fish(cv, x, y, d, c, eye=(10, 10, 20)):
    cv.rect(x - 3, y - 1, 6, 3, c)
    cv.rect(x - 2, y - 2, 4, 1, c)
    cv.rect(x - 2, y + 2, 4, 1, c)
    cv.rect(x - 4 * d - (1 if d > 0 else 0), y - 2, 2, 5, c)
    cv.put(x + 2 * d, y - 1, eye)


def water(cv, stops):
    """Vertical gradient through (position, color) stops."""
    for y in range(cv.n):
        k = y / cv.n
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= k <= p1:
                cv.row(y, mix(c0, c1, (k - p0) / (p1 - p0) if p1 > p0 else 0))
                break


def sand(cv, c1, c2):
    cv.rect(0, cv.n - 5, cv.n, 5, c1)
    cv.rect(0, cv.n - 6, cv.n, 1, c2)


def make_weeds(rng):
    return [dict(x=round(N * (0.12 + i * 0.19)), h=rng.randint(int(N * 0.2), int(N * 0.42)), ph=rng.uniform(0, 6))
            for i in range(5)]


def weeds(cv, lst, u, col):
    for w in lst:
        for i in range(w["h"]):
            sway = round(math.sin(TAU * 2 * u + w["ph"] + i * 0.25) * (i / w["h"]) * 2.5)
            cv.put(w["x"] + sway, cv.n - 6 - i, (col[0], max(0, col[1] - i), col[2]))


def make_fish(rng, speeds, colors, ymin=12, ymax=42):
    """speeds are integer wrap counts per loop, so every fish returns to its start."""
    return [dict(x=rng.uniform(0, N), y=rng.uniform(ymin, ymax), k=k * (1 if i % 2 else -1), c=colors[i % len(colors)],
                 ph=rng.uniform(0, 6)) for i, k in enumerate(speeds)]


def fish_pos(f, frame, frames, u, bob):
    x = round((f["x"] + PATH * f["k"] * frame / frames) % PATH - 6)
    y = round(f["y"] + math.sin(TAU * 3 * u + f["ph"]) * bob)
    return x, y, (1 if f["k"] > 0 else -1)


def make_bubbles(rng, count, speeds):
    return [dict(x=rng.randint(4, N - 5), y0=rng.uniform(0, N - 7), k=speeds[i % len(speeds)]) for i in range(count)]


def bubbles(cv, lst, frame, frames, c1, c2):
    h = N - 7
    for b in lst:
        y = round((b["y0"] - h * b["k"] * frame / frames) % h)
        cv.put(b["x"], y, c1)
        cv.put(b["x"] + 1, y + 1, c2)


# ---------------------------------------------------------------- the four scenes
def scene_morning(frames):
    rng = random.Random(1)
    fish = make_fish(rng, [2, 2, 3, 2], [(255, 150, 60), (90, 200, 255), (255, 110, 130), (240, 220, 90)])
    bub, wd = make_bubbles(rng, 5, [2, 2, 3, 2, 3]), make_weeds(rng)
    for f in range(frames):
        u = f / frames
        cv = Canvas(N)
        water(cv, [(0, (252, 140, 70)), (0.18, (240, 160, 100)), (0.4, (110, 165, 185)), (1, (25, 90, 150))])
        for r in range(4):  # warm rays from the top left, drifting
            x0 = 4 + r * 14 + round(math.sin(TAU * u + r) * 2)
            for y in range(42):
                x = x0 + round(y * 0.45)
                k = 0.5 * (1 - y / 42)
                for w in range(3):
                    cv.blend(x + w, y, (255, 200, 100), k * (1 if w == 1 else 0.6))
        sand(cv, (196, 170, 110), (160, 140, 90))
        weeds(cv, wd, u, (40, 160, 70))
        for fs in fish:
            x, y, d = fish_pos(fs, f, frames, u, 1.5)
            draw_fish(cv, x, y, d, fs["c"])
        bubbles(cv, bub, f, frames, (210, 235, 255), (130, 180, 225))
        yield cv.image()


def scene_day(frames):
    rng = random.Random(2)
    fish = make_fish(rng, [2, 2, 3, 2, 2], [(255, 140, 40), (80, 200, 255), (255, 90, 120), (240, 220, 80), (120, 240, 160)])
    bub, wd = make_bubbles(rng, 8, [2, 2, 3, 2]), make_weeds(rng)
    for f in range(frames):
        u = f / frames
        cv = Canvas(N)
        water(cv, [(0, (40, 190, 210)), (1, (10, 90, 150))])
        for x in range(N):  # caustic shimmer below the surface
            y = 2 + round(2 * math.sin(x * 0.5 + TAU * 3 * u) + math.sin(x * 0.23 - TAU * 2 * u))
            cv.put(x, y, (200, 245, 250))
            cv.blend(x, y + 1, (200, 245, 250), 0.5)
        sand(cv, (214, 190, 130), (170, 150, 100))
        weeds(cv, wd, u, (30, 170, 60))
        for fs in fish:
            x, y, d = fish_pos(fs, f, frames, u, 2)
            draw_fish(cv, x, y, d, fs["c"])
        bubbles(cv, bub, f, frames, (230, 250, 255), (140, 200, 235))
        yield cv.image()


def scene_evening(frames):
    rng = random.Random(3)
    fish = make_fish(rng, [2, 2, 2], [(240, 120, 50), (150, 90, 170), (250, 160, 70)])
    bub, wd = make_bubbles(rng, 3, [2]), make_weeds(rng)
    for f in range(frames):
        u = f / frames
        cv = Canvas(N)
        water(cv, [(0, (255, 110, 40)), (0.2, (235, 95, 70)), (0.45, (150, 60, 110)), (1, (20, 25, 70))])
        sand(cv, (150, 120, 90), (110, 90, 70))
        weeds(cv, wd, u, (30, 100, 60))
        for fs in fish:
            x, y, d = fish_pos(fs, f, frames, u, 1)
            draw_fish(cv, x, y, d, fs["c"], (30, 20, 30))
        bubbles(cv, bub, f, frames, (240, 200, 180), (160, 120, 140))
        yield cv.image()


def scene_night(frames):
    rng = random.Random(4)
    base = [(255, 140, 40), (80, 200, 255), (255, 90, 120), (240, 220, 80)]
    fish = [dict(x=10 + i * 13, y=N - 9, c=mix(c, (20, 25, 50), 0.55), d=(1 if i % 2 else -1), ph=rng.uniform(0, 6))
            for i, c in enumerate(base)]
    plankton = [dict(x=rng.randint(2, N - 3), y=rng.randint(4, N - 12), ph=rng.uniform(0, 6)) for _ in range(14)]
    bub, wd = make_bubbles(rng, 2, [1, 2]), make_weeds(rng)
    for f in range(frames):
        u = f / frames
        cv = Canvas(N)
        water(cv, [(0, (12, 18, 50)), (1, (4, 8, 24))])
        cv.rect(0, N - 5, N, 5, (60, 58, 60))
        cv.rect(0, N - 6, N, 1, (48, 46, 50))
        weeds(cv, wd, u, (15, 60, 40))
        for fs in fish:  # resting on the sand, breathing
            y = fs["y"] + (-1 if math.sin(TAU * 4 * u + fs["ph"]) > 0.7 else 0)
            draw_fish(cv, fs["x"], y, fs["d"], fs["c"], (40, 45, 70))
        for q in plankton:
            b = 0.5 + 0.5 * math.sin(TAU * 6 * u + q["ph"])
            if b > 0.55:
                cv.put(q["x"], q["y"], mix((40, 70, 110), (160, 230, 220), (b - 0.55) / 0.45))
        bubbles(cv, bub, f, frames, (150, 180, 220), (80, 100, 150))
        yield cv.image()


SCENES = {"morning": scene_morning, "day": scene_day, "evening": scene_evening, "night": scene_night}
# Speed relative to the day scene: same distance per loop, so a longer loop means slower motion
LOOP_FACTOR = {"morning": 1.25, "day": 1.0, "evening": 1.6667, "night": 2.0}


# ---------------------------------------------------------------- encoding
def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def nearest(frames, pal):
    pal = np.array(pal, dtype=np.int32).reshape(-1, 3)
    out = []
    for fr in frames:
        px = np.array(fr, dtype=np.int32).reshape(-1, 3)
        dist = ((px[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
        out.append(dist.argmin(axis=1).astype(np.uint8).reshape(fr.height, fr.width))
    return pal.flatten().tolist(), out, len(pal)


def build_palette(frames, max_colors):
    """Exact colors when they fit, otherwise a median-cut palette shared by all frames."""
    colors = {}
    for fr in frames:
        for c in np.array(fr).reshape(-1, 3):
            colors.setdefault(tuple(int(v) for v in c), len(colors))
            if len(colors) > max_colors:
                break
        if len(colors) > max_colors:
            break
    if len(colors) <= max_colors:
        return nearest(frames, list(colors.keys()))
    sample = frames[:: max(1, len(frames) // 12)]
    strip = Image.new("RGB", (sample[0].width * len(sample), sample[0].height))
    for i, fr in enumerate(sample):
        strip.paste(fr, (i * fr.width, 0))
    pal_img = strip.quantize(colors=max_colors, method=Image.MEDIANCUT, dither=Image.NONE)
    used = min(max_colors, int(np.array(pal_img).max()) + 1)
    return nearest(frames, pal_img.getpalette()[: used * 3])


def rle(flat):
    out = bytearray()
    i, n = 0, len(flat)
    while i < n:
        v = flat[i]
        j = i + 1
        while j < n and j - i < 256 and flat[j] == v:
            j += 1
        out += bytes((j - i - 1, int(v)))
        i = j
    return bytes(out)


def encode(frames, scale, fps, out, use_rle=True, bpp=None):
    w, h = frames[0].size
    if w * scale > 240 or h * scale > 240:
        raise SystemExit(f"{w}x{h} at scale {scale} exceeds 240x240")
    if use_rle:
        bpp = 8
    pal, indexed, used = build_palette(frames, 255)  # colorCount is one byte
    if bpp is None:
        bpp = 4 if used <= 16 else 8
    if bpp == 4 and used > 16:
        pal, indexed, used = build_palette(frames, 16)

    data = bytearray(MAGIC)
    data += struct.pack("<HHBBHBBBB", w, h, scale, fps, len(frames), bpp, used, FLAG_RLE if use_rle else 0, 0)
    for i in range(used):
        data += struct.pack("<H", rgb565(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]))
    raw_bytes = 0
    for arr in indexed:
        flat = arr.flatten()
        if use_rle:
            data += rle(flat)
        elif bpp == 4:
            if len(flat) % 2:
                flat = np.append(flat, 0)
            data += bytes((flat[0::2] << 4) | flat[1::2])
        else:
            data += bytes(flat)
        raw_bytes += math.ceil(w * h * bpp / 8)

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f"{out.name}: {len(data)} bytes ({len(data) * 100 // max(raw_bytes, 1)}% of raw), {w}x{h} x{scale} @ {fps} fps, "
          f"{len(frames)} frames, {used} colors, {'rle' if use_rle else f'{bpp} bpp'}")


def decode(path):
    data = path.read_bytes()
    magic = data[:4]
    if magic not in (MAGIC, MAGIC_RAW):
        raise SystemExit("not a PXA file")
    w, h, scale, fps, count, bpp, used, flags, _ = struct.unpack_from("<HHBBHBBBB", data, 4)
    if magic == MAGIC_RAW:
        flags = 0
    pos = 16
    pal = []
    for _ in range(used):
        (c,) = struct.unpack_from("<H", data, pos)
        pos += 2
        pal.append(((c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3))
    pal = np.array(pal, dtype=np.uint8)
    frames = []
    for _ in range(count):
        if flags & FLAG_RLE:
            idx = np.empty(w * h, dtype=np.uint8)
            filled = 0
            while filled < w * h:
                run, v = data[pos], data[pos + 1]
                pos += 2
                idx[filled: filled + run + 1] = v
                filled += run + 1
        elif bpp == 4:
            per = math.ceil(w * h / 2)
            raw = np.frombuffer(data[pos: pos + per], dtype=np.uint8)
            pos += per
            idx = np.stack([raw >> 4, raw & 0x0F], axis=1).flatten()[: w * h]
        else:
            idx = np.frombuffer(data[pos: pos + w * h], dtype=np.uint8)
            pos += w * h
        rgb = pal[np.minimum(idx, used - 1)].reshape(h, w, 3)
        frames.append(Image.fromarray(rgb, "RGB").resize((w * scale, h * scale), Image.NEAREST))
    return frames, fps


def save_gif(frames, fps, out):
    pal_img = frames[0].quantize(colors=255, method=Image.MEDIANCUT, dither=Image.NONE)
    q = [fr.quantize(palette=pal_img, dither=Image.NONE) for fr in frames]
    q[0].save(out, save_all=True, append_images=q[1:], duration=round(1000 / fps), loop=0, optimize=True)


# ---------------------------------------------------------------- cli
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("scenes", help="render the four daytime scenes into data/gif/")
    s.add_argument("--frames", type=int, default=192)
    s.add_argument("--fps", type=int, default=24)
    s.add_argument("--only", choices=list(SCENES), nargs="*")

    g = sub.add_parser("from-gif")
    g.add_argument("input", type=pathlib.Path)
    g.add_argument("--grid", type=int, default=60, help="target grid edge length")
    g.add_argument("--scale", type=int, default=4)
    g.add_argument("--fps", type=int, default=0, help="0 = take from the GIF")
    g.add_argument("--no-rle", action="store_true", help="write raw frames (PXA1-compatible layout)")
    g.add_argument("--out", type=pathlib.Path)

    t = sub.add_parser("to-gif")
    t.add_argument("input", type=pathlib.Path)
    t.add_argument("--out", type=pathlib.Path)

    r = sub.add_parser("readme-gifs", help="four seconds of every scene as one GIF each")
    r.add_argument("--out-dir", type=pathlib.Path, default=HERE.parent / "docs")

    args = ap.parse_args()

    if args.cmd == "scenes":
        for name in args.only or SCENES:
            frames = list(SCENES[name](round(args.frames * LOOP_FACTOR[name])))
            encode(frames, 240 // N, args.fps, DATA_GIF / f"{name}.pxa")
    elif args.cmd == "from-gif":
        im = Image.open(args.input)
        frames, fps = [], args.fps
        for i in range(getattr(im, "n_frames", 1)):
            im.seek(i)
            if not fps:
                fps = max(1, round(1000 / (im.info.get("duration") or 100)))
            frames.append(im.convert("RGB").resize((args.grid, args.grid), Image.LANCZOS))
        encode(frames, args.scale, fps, args.out or args.input.with_suffix(".pxa"), use_rle=not args.no_rle)
    elif args.cmd == "to-gif":
        frames, fps = decode(args.input)
        out = args.out or args.input.with_suffix(".check.gif")
        save_gif(frames, fps, out)
        print(f"{out}: {len(frames)} frames @ {fps} fps")
    elif args.cmd == "readme-gifs":
        args.out_dir.mkdir(parents=True, exist_ok=True)
        for name in SCENES:
            # the full loop at the pace the device plays it (192 frames x loop factor at 24 fps),
            # shown with every second frame at 12 fps and at 3x pixel size to keep the files small
            frames = list(SCENES[name](round(192 * LOOP_FACTOR[name])))[::2]
            out = args.out_dir / f"{name}.gif"
            save_gif([fr.resize((180, 180), Image.NEAREST) for fr in frames], 12, out)
            print(f"{out.name}: {len(frames)} frames, {out.stat().st_size} bytes")


if __name__ == "__main__":
    main()
