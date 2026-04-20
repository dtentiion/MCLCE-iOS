#!/usr/bin/env python3
"""
Minimal LCE .arc archive file lister.

Format (big-endian throughout, as documented in DexrnZacAttack/libLCE):
    uint32  file_count
    repeat file_count times:
        uint16  name_len
        bytes   name (Windows path with backslashes)
        uint32  data_offset
        uint32  data_size

The file data blobs live at their recorded offsets further in the file.

Usage:
    python scripts/list-arc.py <archive.arc>
    python scripts/list-arc.py <archive.arc> --extract <outdir>
    python scripts/list-arc.py <archive.arc> --count-extensions
"""

from __future__ import annotations
import argparse
import os
import struct
import sys
from collections import Counter
from pathlib import Path


def read_entries(data: bytes):
    pos = 0
    file_count = struct.unpack_from(">I", data, pos)[0]
    pos += 4
    entries = []
    for _ in range(file_count):
        name_len = struct.unpack_from(">H", data, pos)[0]
        pos += 2
        name = data[pos : pos + name_len].decode("utf-8", errors="replace")
        pos += name_len
        offset = struct.unpack_from(">I", data, pos)[0]
        pos += 4
        size = struct.unpack_from(">I", data, pos)[0]
        pos += 4
        entries.append((name.replace("\\", "/"), offset, size))
    return entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("archive")
    ap.add_argument("--extract", metavar="DIR", default=None)
    ap.add_argument("--count-extensions", action="store_true")
    ap.add_argument("--filter", default=None,
                    help="Only list entries whose path contains this substring.")
    args = ap.parse_args()

    data = Path(args.archive).read_bytes()
    entries = read_entries(data)

    print(f"Archive {args.archive}: {len(entries)} files, {len(data):,} bytes total")

    if args.count_extensions:
        counter = Counter()
        for name, _, size in entries:
            ext = os.path.splitext(name)[1].lower() or "<none>"
            counter[ext] += 1
        for ext, n in sorted(counter.items(), key=lambda kv: -kv[1]):
            print(f"  {ext:10s} {n:6d}")
        return

    shown = 0
    for name, offset, size in entries:
        if args.filter and args.filter.lower() not in name.lower():
            continue
        print(f"  {size:10d}  {name}")
        if args.extract:
            blob = data[offset : offset + size]
            out = Path(args.extract) / name
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(blob)
        shown += 1

    if args.filter:
        print(f"matched {shown} / {len(entries)}")


if __name__ == "__main__":
    sys.exit(main() or 0)
