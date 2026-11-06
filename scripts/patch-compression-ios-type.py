#!/usr/bin/env python3
"""APPROPRIATE_COMPRESSION_TYPE is zlib on iOS.

compression.cpp already runs iOS through the zlib code like Windows64, but
compression.h still names LZX for it. Game rules and schematic files are
written with that type and read back by it, and ConsoleSchematicFile puts
the thread's decompression type back to it after an unusual file. With LZX
there, iOS would tag its zlib data as LZX and stop decompressing.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HDR = REPO / "upstream" / "Minecraft.World" / "compression.h"

if not HDR.exists():
    sys.exit(f"missing: {HDR}")
src = HDR.read_text(encoding="utf-8", errors="replace")
OLD = ("#if defined __ORBIS__ || defined _DURANGO || defined _WIN64 || defined __PSVITA__\n"
       "#define APPROPRIATE_COMPRESSION_TYPE Compression::eCompressionType_ZLIBRLE")
NEW = ("#if defined __ORBIS__ || defined _DURANGO || defined _WIN64 || defined __PSVITA__ || defined __APPLE_IOS__\n"
       "#define APPROPRIATE_COMPRESSION_TYPE Compression::eCompressionType_ZLIBRLE")
if NEW in src:
    print(f"already patched: {HDR}")
    sys.exit(0)
if src.count(OLD) != 1:
    sys.exit(f"anchor not found once in {HDR}")
HDR.write_text(src.replace(OLD, NEW), encoding="utf-8")
print(f"patched: {HDR}")
