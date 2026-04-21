#!/usr/bin/env python3
"""
Minimal SWF parser for dumping PlaceObject2/3 tag headers only.

Tells us what combination of PlaceObject flags 4J's authored SWFs actually
emit, so we can decide whether Ruffle's "Invalid PlaceObject type" leniency
patch is the right approach or whether the interpretation is subtler.

Usage:
    python scripts/dump-po3.py <path.swf>
"""

from __future__ import annotations
import struct
import sys
import zlib
from pathlib import Path


def inflate_if_needed(data: bytes) -> bytes:
    sig = data[:3]
    if sig == b"FWS":
        return data
    if sig == b"CWS":
        # zlib compressed starting at byte 8.
        return data[:8] + zlib.decompress(data[8:])
    if sig == b"ZWS":
        raise SystemExit("LZMA SWF not supported by this helper")
    raise SystemExit(f"Not a SWF: signature {sig!r}")


def read_ub(buf: bytes, bit_offset: int, nbits: int):
    value = 0
    for _ in range(nbits):
        byte_index = bit_offset >> 3
        bit_index = 7 - (bit_offset & 7)
        value = (value << 1) | ((buf[byte_index] >> bit_index) & 1)
        bit_offset += 1
    return value, bit_offset


def skip_rect(buf: bytes, offset: int) -> int:
    # Skip the FrameSize RECT.
    bit_offset = offset * 8
    nbits, bit_offset = read_ub(buf, bit_offset, 5)
    bit_offset += nbits * 4
    byte_offset = (bit_offset + 7) >> 3
    return byte_offset


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        sys.exit(2)

    raw = Path(sys.argv[1]).read_bytes()
    data = inflate_if_needed(raw)

    version = data[3]
    body = data[8:]  # skip FWS + version + file length
    off = skip_rect(body, 0)
    off += 4  # frame rate (u16) + frame count (u16)

    print(f"SWF version {version}, body starts at file offset {8 + off}")

    tag_count = 0
    po3_count = 0
    po3_invalid = 0
    while off < len(body):
        if off + 2 > len(body):
            break
        record_header = struct.unpack_from("<H", body, off)[0]
        tag_code = record_header >> 6
        tag_len = record_header & 0x3F
        off += 2
        if tag_len == 0x3F:
            tag_len = struct.unpack_from("<I", body, off)[0]
            off += 4
        tag_end = off + tag_len

        if tag_code == 0:  # End
            break

        # PlaceObject2 = 26, PlaceObject3 = 70, PlaceObject4 = 94
        if tag_code in (26, 70, 94):
            label = {26: "PO2", 70: "PO3", 94: "PO4"}[tag_code]
            if tag_code >= 70:
                flags_u16 = struct.unpack_from("<H", body, off)[0]
                flag_bytes = 2
            else:
                flags_u16 = body[off]
                flag_bytes = 1
            depth = struct.unpack_from("<H", body, off + flag_bytes)[0]
            has_move = bool(flags_u16 & (1 << 0))
            has_char = bool(flags_u16 & (1 << 1))
            has_matrix = bool(flags_u16 & (1 << 2))
            has_name = bool(flags_u16 & (1 << 5))
            has_filter = bool(flags_u16 & (1 << 8))
            has_blend = bool(flags_u16 & (1 << 9))
            has_cache = bool(flags_u16 & (1 << 10))
            has_class = bool(flags_u16 & (1 << 11))
            has_image = bool(flags_u16 & (1 << 12))
            has_visible = bool(flags_u16 & (1 << 13))
            invalid = (not has_move) and (not has_char)
            if tag_code == 70:
                po3_count += 1
                if invalid:
                    po3_invalid += 1
            flags_str = (
                f"MOVE={int(has_move)} HAS_CHAR={int(has_char)} "
                f"HAS_MATRIX={int(has_matrix)} HAS_NAME={int(has_name)} "
                f"HAS_FILTER={int(has_filter)} HAS_BLEND={int(has_blend)} "
                f"HAS_CACHE={int(has_cache)} HAS_CLASS={int(has_class)} "
                f"HAS_IMAGE={int(has_image)} HAS_VISIBLE={int(has_visible)}"
            )
            marker = "  !! invalid (no MOVE, no CHAR)" if invalid else ""
            print(
                f"offset=0x{off - (2 if tag_len < 0x3F else 6):06X} "
                f"{label} tag_len={tag_len:5d} depth={depth:4d} "
                f"flags=0x{flags_u16:04X}{marker}"
            )
            # Dump raw next 16 bytes after flags+depth for further analysis.
            trailing = body[off + flag_bytes + 2 : off + flag_bytes + 2 + 16]
            print(f"  trailing16: {trailing.hex()}")
            print(f"  {flags_str}")

        tag_count += 1
        off = tag_end

    print(f"-- total tags: {tag_count}")
    print(f"-- PlaceObject3 count: {po3_count}, invalid (no MOVE, no CHAR): {po3_invalid}")


if __name__ == "__main__":
    main()
