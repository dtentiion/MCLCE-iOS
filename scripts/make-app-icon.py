#!/usr/bin/env python3
"""The app icon, made from the Windows64 build's.

upstream/Minecraft.Client/Xbox/MinecraftWindows.ico is the icon the
Windows64 exe carries. Its 256x256 image is put on black (iOS icons have no
transparency) and written out at the sizes Info.plist names, into
Minecraft.Client/iOS/Resources. The PNGs are build output, the image itself
stays out of this repo.

Standard library only.
"""
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ICO = REPO / "upstream" / "Minecraft.Client" / "Xbox" / "MinecraftWindows.ico"
OUT = REPO / "Minecraft.Client" / "iOS" / "Resources"

# File name and size in pixels
ICONS = [
    ("AppIcon60x60@2x.png", 120),
    ("AppIcon60x60@3x.png", 180),
    ("AppIcon76x76@2x~ipad.png", 152),
    ("AppIcon83.5x83.5@2x~ipad.png", 167),
]

# How much of the icon the block takes up, the rest is the black around it
FILL = 0.82


def read_ico(path):
    """The 256x256 32-bit image as rows of (r, g, b, a), top row first."""
    data = path.read_bytes()
    count = struct.unpack_from("<H", data, 4)[0]
    offset = None
    for i in range(count):
        width, _, _, _, _, bits, _, where = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        # 256 is stored as 0
        if width == 0 and bits == 32:
            offset = where
    if offset is None:
        sys.exit(f"no 256x256 image in {path}")
    # An uncompressed bitmap, bottom row first, BGRA
    pixels = offset + struct.unpack_from("<I", data, offset)[0]
    rows = []
    for y in range(255, -1, -1):
        row = []
        for x in range(256):
            b, g, r, a = data[pixels + (y * 256 + x) * 4: pixels + (y * 256 + x) * 4 + 4]
            row.append((r, g, b, a))
        rows.append(row)
    return rows


def scale(rows, size):
    """Area-averaged down to size x size, alpha premultiplied for the average."""
    src = len(rows)
    out = []
    for y in range(size):
        y0, y1 = y * src / size, (y + 1) * src / size
        row = []
        for x in range(size):
            x0, x1 = x * src / size, (x + 1) * src / size
            r = g = b = a = total = 0.0
            sy = int(y0)
            while sy < y1 and sy < src:
                wy = min(y1, sy + 1) - max(y0, sy)
                sx = int(x0)
                while sx < x1 and sx < src:
                    w = wy * (min(x1, sx + 1) - max(x0, sx))
                    pr, pg, pb, pa = rows[sy][sx]
                    r += pr * pa * w
                    g += pg * pa * w
                    b += pb * pa * w
                    a += pa * w
                    total += w
                    sx += 1
                sy += 1
            if a > 0:
                row.append((r / a, g / a, b / a, a / total))
            else:
                row.append((0.0, 0.0, 0.0, 0.0))
        out.append(row)
    return out


def write_png(path, rows):
    """Opaque RGB rows to a PNG."""
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for r, g, b in row:
            raw += bytes((r, g, b))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    size = len(rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    if not ICO.exists():
        sys.exit(f"missing: {ICO}")
    source = read_ico(ICO)
    for name, size in ICONS:
        inner = round(size * FILL)
        block = scale(source, inner)
        margin = (size - inner) // 2
        rows = [[(0, 0, 0)] * size for _ in range(size)]
        for y in range(inner):
            for x in range(inner):
                r, g, b, a = block[y][x]
                alpha = a / 255.0
                rows[margin + y][margin + x] = (round(r * alpha), round(g * alpha), round(b * alpha))
        write_png(OUT / name, rows)
        print(f"wrote {OUT / name}")


if __name__ == "__main__":
    main()
