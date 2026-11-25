#!/usr/bin/env python3
"""Generate the two 32x32 T0 image source frames as raw PPM (P6).

The ffmpeg drawbox/color filters apply level tweaks (pure red lands at 253,
not 255), so the source frames are written byte-exact here and then converted
to the committed fixtures (PNG/JPEG/BMP/GIF/WebP) by ffmpeg.

Layout (16x16 quadrants, 32x32 canvas):
  frame A: TL red / TR green / BL blue / BR white
  frame B: TL blue / TR red  / BL green / BR white

Usage:
  python gen_t0_ppm.py [out_dir]
Default out_dir is the current directory. Writes frame_a.ppm / frame_b.ppm.
"""
import os
import struct
import sys

W = 32
H = 32

RED = (255, 0, 0)
GREEN = (0, 255, 0)
BLUE = (0, 0, 255)
WHITE = (255, 255, 255)


def quadrant(x: int, y: int):
    left = x < W // 2
    top = y < H // 2
    return left, top


def pixel(frame: int, x: int, y: int):
    left, top = quadrant(x, y)
    if frame == 0:  # A
        if top and left:
            return RED
        if top and not left:
            return GREEN
        if not top and left:
            return BLUE
        return WHITE
    # B
    if top and left:
        return BLUE
    if top and not left:
        return RED
    if not top and left:
        return GREEN
    return WHITE


def write_ppm(path: str, frame: int) -> None:
    rows = bytearray()
    for y in range(H):
        for x in range(W):
            rows += bytes(pixel(frame, x, y))
    header = f"P6\n{W} {H}\n255\n".encode("ascii")
    with open(path, "wb") as f:
        f.write(header)
        f.write(rows)


def main() -> int:
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out_dir, exist_ok=True)
    write_ppm(os.path.join(out_dir, "frame_a.ppm"), 0)
    write_ppm(os.path.join(out_dir, "frame_b.ppm"), 1)
    print("wrote frame_a.ppm frame_b.ppm to", out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
