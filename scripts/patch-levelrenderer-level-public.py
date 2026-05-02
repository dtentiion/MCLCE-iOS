#!/usr/bin/env python3
"""G3e: expose LevelRenderer::level[4] so MCLEGameLoop can set it
without going through setLevel + allChanged (which allocates the chunk
mesh array and is the wrong shape for our shim path right now).

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.h"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")

old = "\tMultiPlayerLevel *level[4];"
new = "public:\t// 4J iOS - shim wires this directly\n\tMultiPlayerLevel *level[4];\nprivate:"

if new in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

patched = src.replace(old, new, 1)
TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
