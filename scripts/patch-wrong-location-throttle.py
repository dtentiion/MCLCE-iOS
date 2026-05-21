#!/usr/bin/env python3
"""Throttle the "Wrong location!" debug print in LevelChunk::addEntity.

Upstream prints it every time an entity's chunk-coords disagree with the
chunk it's being added to. It's benign info but bursts to hundreds of
lines in short windows, contributing to os_log backpressure that has
bitten us before.

Cap to the first 16 occurrences per process, then count silently.
Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.World" / "LevelChunk.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_WL_THROTTLE" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "\tif (xc != this->x || zc != this->z)\n"
    "\t{\n"
    "\t\tapp.DebugPrintf(\"Wrong location!\");\n"
)
new = (
    "\tif (xc != this->x || zc != this->z)\n"
    "\t{\n"
    "\t\t// MCLE_WL_THROTTLE: first 16 only, then silent count.\n"
    "\t\tstatic std::atomic<int> _wlCount{0};\n"
    "\t\tconst int _n = _wlCount.fetch_add(1, std::memory_order_relaxed);\n"
    "\t\tif (_n < 16) app.DebugPrintf(\"Wrong location! (occurrence %d)\", _n);\n"
)
if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)

# Make sure <atomic> is pulled in. LevelChunk.cpp doesn't include it
# directly in stock upstream; insert near the top after the stdafx.
include_anchor = '#include "stdafx.h"\n'
include_addition = (
    '#include "stdafx.h"\n'
    "#ifdef __APPLE_IOS__\n"
    "#include <atomic>\n"
    "#endif\n"
)
if include_anchor in src:
    src = src.replace(include_anchor, include_addition, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
