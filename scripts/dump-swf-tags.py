#!/usr/bin/env python3
"""
Dump a summary of a SWF's tag table: ImportAssets(2), SymbolClass,
ExportAssets, DoAbc(2), and basic character define tags.

Tells us whether the SWF relies on ImportAssets2 (and what the
linkage names look like) vs having its classes baked in via DoAbc2
and SymbolClass tags.

Usage:
    python scripts/dump-swf-tags.py <path.swf>
"""

from __future__ import annotations
import struct
import sys
import zlib
from pathlib import Path


def inflate(data: bytes) -> bytes:
    sig = data[:3]
    if sig == b"FWS":
        return data
    if sig == b"CWS":
        return data[:8] + zlib.decompress(data[8:])
    raise SystemExit(f"Not a SWF or unsupported compression: {sig!r}")


def read_ub(buf, bit_offset, nbits):
    value = 0
    for _ in range(nbits):
        byte_index = bit_offset >> 3
        bit_index = 7 - (bit_offset & 7)
        value = (value << 1) | ((buf[byte_index] >> bit_index) & 1)
        bit_offset += 1
    return value, bit_offset


def skip_rect(buf, offset):
    bit_offset = offset * 8
    nbits, bit_offset = read_ub(buf, bit_offset, 5)
    bit_offset += nbits * 4
    return (bit_offset + 7) >> 3


def read_cstr(buf, off):
    end = buf.index(0, off)
    return buf[off:end], end + 1


TAGS = {
    0: "End",
    9: "SetBackgroundColor",
    26: "PlaceObject2",
    43: "FrameLabel",
    56: "ExportAssets",
    57: "ImportAssets",
    59: "DoInitAction",
    65: "ScriptLimits",
    69: "FileAttributes",
    70: "PlaceObject3",
    71: "ImportAssets2",
    72: "DoABC",
    73: "DefineFontAlignZones",
    74: "CSMTextSettings",
    75: "DefineFont3",
    76: "SymbolClass",
    77: "Metadata",
    78: "DefineScalingGrid",
    82: "DoABC2",
    83: "DefineShape4",
    86: "DefineSceneAndFrameLabelData",
    88: "DefineFontName",
    94: "PlaceObject4",
    2: "DefineShape",
    22: "DefineShape2",
    32: "DefineShape3",
    1: "ShowFrame",
}


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        sys.exit(2)

    raw = Path(sys.argv[1]).read_bytes()
    data = inflate(raw)
    version = data[3]
    body = data[8:]
    off = skip_rect(body, 0)
    off += 4

    print(f"SWF v{version}, header ends at file offset {8+off}")

    counts = {}
    while off < len(body):
        if off + 2 > len(body):
            break
        hdr = struct.unpack_from("<H", body, off)[0]
        code = hdr >> 6
        length = hdr & 0x3F
        off += 2
        if length == 0x3F:
            length = struct.unpack_from("<I", body, off)[0]
            off += 4
        end = off + length
        name = TAGS.get(code, f"Tag{code}")
        counts[name] = counts.get(name, 0) + 1

        if code == 56:  # ExportAssets
            count = struct.unpack_from("<H", body, off)[0]
            cursor = off + 2
            print(f"ExportAssets: {count} entries")
            for _ in range(count):
                cid = struct.unpack_from("<H", body, cursor)[0]
                cursor += 2
                s, cursor = read_cstr(body, cursor)
                print(f"  id={cid:5d} name={s!r}")
        elif code == 57:  # ImportAssets
            url, cursor = read_cstr(body, off)
            count = struct.unpack_from("<H", body, cursor)[0]
            cursor += 2
            print(f"ImportAssets url={url!r} count={count}")
            for _ in range(count):
                cid = struct.unpack_from("<H", body, cursor)[0]
                cursor += 2
                s, cursor = read_cstr(body, cursor)
                print(f"  id={cid:5d} name={s!r}")
        elif code == 71:  # ImportAssets2
            url, cursor = read_cstr(body, off)
            cursor += 2  # two reserved bytes
            count = struct.unpack_from("<H", body, cursor)[0]
            cursor += 2
            print(f"ImportAssets2 url={url!r} count={count}")
            for _ in range(count):
                cid = struct.unpack_from("<H", body, cursor)[0]
                cursor += 2
                s, cursor = read_cstr(body, cursor)
                print(f"  id={cid:5d} name={s!r}")
        elif code == 76:  # SymbolClass
            count = struct.unpack_from("<H", body, off)[0]
            cursor = off + 2
            print(f"SymbolClass: {count} entries")
            for _ in range(count):
                cid = struct.unpack_from("<H", body, cursor)[0]
                cursor += 2
                s, cursor = read_cstr(body, cursor)
                print(f"  id={cid:5d} class={s!r}")
        elif code == 82:  # DoABC2
            flags = struct.unpack_from("<I", body, off)[0]
            name, _ = read_cstr(body, off + 4)
            print(f"DoABC2 flags=0x{flags:08x} name={name!r} abc_bytes={length}")
        elif code == 72:  # DoABC
            print(f"DoABC len={length}")

        if code == 0:
            break
        off = end

    print("-- tag counts:")
    for k, v in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(f"  {v:5d}  {k}")


if __name__ == "__main__":
    main()
