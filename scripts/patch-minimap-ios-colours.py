#!/usr/bin/env python3
"""Map colours on iOS use the Windows64 byte order.

Minimap::reloadColours builds its colour table in the byte order the
platform's texture upload wants. iOS is little endian and uploads RGBA
like Windows64, Durango and Vita, but falls into the big-endian arm,
so every map colour came out with its channels swapped.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Minimap.cpp"

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
src = SRC.read_text(encoding="utf-8", errors="replace")
OLD = "#if ( defined _DURANGO || defined _WIN64 || __PSVITA__ )"
NEW = "#if ( defined _DURANGO || defined _WIN64 || __PSVITA__ || defined __APPLE_IOS__ )"
if NEW in src:
    print(f"already patched: {SRC}")
    sys.exit(0)
if src.count(OLD) != 1:
    sys.exit(f"anchor not found once in {SRC}")
SRC.write_text(src.replace(OLD, NEW), encoding="utf-8")
print(f"patched: {SRC}")
