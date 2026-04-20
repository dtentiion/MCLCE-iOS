#!/usr/bin/env python3
"""
Generate a minimal SWF file that draws a solid-colored rectangle.

Used as our canonical smoke-test movie for the Metal render_handler. Unlike
SWFs that depend on text/fonts (FreeType not wired yet on iOS), this movie
guarantees draw_mesh_strip activity through the render_handler.

Output: a single uncompressed SWF (FWS, version 6), 640x480 stage, one
frame showing a teal rectangle centered on the canvas.

Usage:
    python3 scripts/gen-test-swf.py out.swf
"""

from __future__ import annotations
import io
import struct
import sys
from pathlib import Path


class BitWriter:
    """Accumulates bits, flushes out bytes. SWF is big-endian bit stream."""

    def __init__(self) -> None:
        self.buf = bytearray()
        self.bit_pos = 0  # 0..7 from MSB of current byte
        self.cur = 0

    def write_bits(self, value: int, nbits: int) -> None:
        for i in range(nbits - 1, -1, -1):
            bit = (value >> i) & 1
            self.cur = (self.cur << 1) | bit
            self.bit_pos += 1
            if self.bit_pos == 8:
                self.buf.append(self.cur)
                self.cur = 0
                self.bit_pos = 0

    def write_sbits(self, value: int, nbits: int) -> None:
        if value < 0:
            value = value + (1 << nbits)
        self.write_bits(value, nbits)

    def byte_align(self) -> None:
        if self.bit_pos != 0:
            self.cur <<= (8 - self.bit_pos)
            self.buf.append(self.cur)
            self.cur = 0
            self.bit_pos = 0

    def bytes(self) -> bytes:
        self.byte_align()
        return bytes(self.buf)


def bits_needed(value: int) -> int:
    """Number of bits needed to represent value as signed SWF int."""
    if value == 0:
        return 0
    if value < 0:
        value = -value - 1
    n = 1
    while (1 << n) <= value:
        n += 1
    return n + 1  # +1 for sign bit


def encode_rect(x_min: int, x_max: int, y_min: int, y_max: int) -> bytes:
    nbits = max(
        bits_needed(x_min),
        bits_needed(x_max),
        bits_needed(y_min),
        bits_needed(y_max),
        1,
    )
    bw = BitWriter()
    bw.write_bits(nbits, 5)
    bw.write_sbits(x_min, nbits)
    bw.write_sbits(x_max, nbits)
    bw.write_sbits(y_min, nbits)
    bw.write_sbits(y_max, nbits)
    return bw.bytes()


def encode_matrix_identity() -> bytes:
    bw = BitWriter()
    bw.write_bits(0, 1)  # HasScale
    bw.write_bits(0, 1)  # HasRotate
    bw.write_bits(0, 5)  # NTranslateBits
    # No translate bits because NTranslateBits is 0.
    return bw.bytes()


def encode_tag(tag_code: int, payload: bytes) -> bytes:
    """Short or long tag record. Long form when >=63 bytes."""
    length = len(payload)
    if length < 0x3F:
        header = struct.pack("<H", (tag_code << 6) | length)
        return header + payload
    header = struct.pack("<HI", (tag_code << 6) | 0x3F, length)
    return header + payload


def make_define_shape(shape_id: int, w_twips: int, h_twips: int,
                      color_rgb: tuple[int, int, int]) -> bytes:
    """DefineShape (tag 2) with a single filled rectangle."""
    out = io.BytesIO()

    # ShapeID (UI16)
    out.write(struct.pack("<H", shape_id))

    # ShapeBounds (RECT)
    out.write(encode_rect(0, w_twips, 0, h_twips))

    # Shape body (bit-packed):
    bw = BitWriter()

    # FillStyleArray: count=1, one solid fill (type 0x00) with RGB color.
    out.write(bytes([1]))  # FillStyleCount
    out.write(bytes([0x00]))  # FILLSTYLE type = solid
    out.write(bytes(color_rgb))  # RGB (3 bytes; DefineShape uses RGB, not RGBA)

    # LineStyleArray: count=0.
    out.write(bytes([0]))

    # ShapeRecords follow, bit-packed.
    # NumFillBits, NumLineBits (4 bits each).
    num_fill_bits = 1  # enough for style index 0 or 1
    num_line_bits = 0
    bw.write_bits(num_fill_bits, 4)
    bw.write_bits(num_line_bits, 4)

    # StyleChangeRecord: set fill style 0 to 1 (our solid fill), move to (0,0)
    bw.write_bits(0, 1)  # TypeFlag = 0 (style change)
    bw.write_bits(0, 1)  # StateNewStyles
    bw.write_bits(0, 1)  # StateLineStyle
    bw.write_bits(1, 1)  # StateFillStyle1
    bw.write_bits(0, 1)  # StateFillStyle0
    bw.write_bits(1, 1)  # StateMoveTo
    mtbits = max(bits_needed(0), bits_needed(w_twips), bits_needed(h_twips), 1)
    bw.write_bits(mtbits, 5)
    bw.write_sbits(0, mtbits)           # MoveDeltaX
    bw.write_sbits(0, mtbits)           # MoveDeltaY
    bw.write_bits(1, num_fill_bits)     # FillStyle1 = 1

    # Straight edges forming a rectangle: right, down, left, up.
    def straight_edge(dx: int, dy: int) -> None:
        # TypeFlag=1 (edge), StraightFlag=1
        bw.write_bits(1, 1)
        bw.write_bits(1, 1)
        # NumBits: SWF stores NumBits field as actual-bits minus 2.
        nbits = max(bits_needed(dx), bits_needed(dy), 2)
        bw.write_bits(nbits - 2, 4)
        # GeneralLineFlag = 1 (both X and Y deltas present)
        bw.write_bits(1, 1)
        bw.write_sbits(dx, nbits)
        bw.write_sbits(dy, nbits)

    straight_edge(w_twips, 0)
    straight_edge(0, h_twips)
    straight_edge(-w_twips, 0)
    straight_edge(0, -h_twips)

    # EndShapeRecord: type=0, flags all 0
    bw.write_bits(0, 6)

    out.write(bw.bytes())
    return out.getvalue()


def make_place_object2(depth: int, shape_id: int,
                       tx_twips: int, ty_twips: int) -> bytes:
    """PlaceObject2 (tag 26) that places `shape_id` at depth with a translate."""
    out = io.BytesIO()
    # Flags byte
    flags = 0
    flags |= 1 << 1  # PlaceFlagHasCharacter
    flags |= 1 << 2  # PlaceFlagHasMatrix
    out.write(bytes([flags]))
    out.write(struct.pack("<H", depth))
    out.write(struct.pack("<H", shape_id))

    # Matrix with translate only.
    bw = BitWriter()
    bw.write_bits(0, 1)  # HasScale
    bw.write_bits(0, 1)  # HasRotate
    ntb = max(bits_needed(tx_twips), bits_needed(ty_twips), 1)
    bw.write_bits(ntb, 5)
    bw.write_sbits(tx_twips, ntb)
    bw.write_sbits(ty_twips, ntb)
    out.write(bw.bytes())
    return out.getvalue()


def make_show_frame() -> bytes:
    return b""


def make_end_tag() -> bytes:
    return b""


def build_swf(width_px: int, height_px: int,
              frame_rate: float, frame_count: int,
              rect_color: tuple[int, int, int]) -> bytes:
    twips = 20
    w = width_px * twips
    h = height_px * twips
    rect_w = w // 3
    rect_h = h // 3
    rect_x = (w - rect_w) // 2
    rect_y = (h - rect_h) // 2

    body = bytearray()
    body.extend(encode_rect(0, w, 0, h))
    body.extend(struct.pack("<H", int(round(frame_rate * 256))))
    body.extend(struct.pack("<H", frame_count))

    # SetBackgroundColor (tag 9): gray-ish so we can see the rect + the clear.
    body.extend(encode_tag(9, bytes([0x18, 0x18, 0x1C])))

    # DefineShape (tag 2)
    body.extend(encode_tag(2, make_define_shape(1, rect_w, rect_h, rect_color)))

    # PlaceObject2 (tag 26)
    body.extend(encode_tag(26, make_place_object2(1, 1, rect_x, rect_y)))

    # ShowFrame (tag 1) x frame_count
    for _ in range(frame_count):
        body.extend(encode_tag(1, make_show_frame()))

    # End (tag 0)
    body.extend(encode_tag(0, make_end_tag()))

    header = bytearray()
    header.extend(b"FWS")         # uncompressed
    header.extend(bytes([6]))     # version 6
    total_len = 8 + len(body)     # 8 = signature + version + length fields
    header.extend(struct.pack("<I", total_len))

    return bytes(header) + bytes(body)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: gen-test-swf.py <output.swf>", file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.parent.mkdir(parents=True, exist_ok=True)
    data = build_swf(
        width_px=640,
        height_px=480,
        frame_rate=30.0,
        frame_count=1,
        rect_color=(0x22, 0xCC, 0xAA),  # teal
    )
    out.write_bytes(data)
    print(f"wrote {out} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
