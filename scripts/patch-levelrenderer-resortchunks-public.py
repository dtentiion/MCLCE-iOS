#!/usr/bin/env python3
"""G5-step16: expose LevelRenderer::resortChunks so the iOS bootstrap
can re-center the render grid around the player after setLevel.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.h"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")

old = "private:\n\tvoid resortChunks(int xc, int yc, int zc);"
new = "public:\t// 4J iOS - shim re-centers grid after setLevel\n\tvoid resortChunks(int xc, int yc, int zc);\nprivate:"

if new in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)
if old not in src:
    sys.exit(f"anchor not found in {TARGET}")
src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
