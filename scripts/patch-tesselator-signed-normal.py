#!/usr/bin/env python3
"""Tesselator::normal packs each component through a signed byte on iOS.

Upstream casts x * 127 straight to `byte` (unsigned char). On x86 that
wraps a negative value round to its two's complement byte, but on ARM a
negative float converts to an unsigned type as 0, so every normal facing
down, west or north lost its sign. The Vita build hit the same thing and
goes through int8_t, which is what iOS takes too.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "Tesselator.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_SIGNED_NORMAL" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "#elif __PSVITA__\n"
    "\t// AP - casting a negative value to 'byte' on Vita results in zero. changed to a signed 8 value\n"
)
new = (
    "// MCLE_iOS_SIGNED_NORMAL: ARM turns a negative float into 0 as an unsigned byte\n"
    "#elif defined(__PSVITA__) || defined(__APPLE_IOS__)\n"
    "\t// AP - casting a negative value to 'byte' on Vita results in zero. changed to a signed 8 value\n"
)

if src.count(old) != 1:
    sys.exit(f"anchor not found in {TARGET}")
src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
