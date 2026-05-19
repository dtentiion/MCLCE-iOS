#!/usr/bin/env python3
"""Null-guard TileEntityRenderDispatcher::instance in Chunk::rebuild.

Upstream calls instance->hasRenderer(et) when iterating entity tiles
in the chunk rebuild loop. Our shim doesn't run Minecraft::init which
calls TileEntityRenderDispatcher::staticCtor() to allocate the
instance, so instance stays nullptr. When the player streams into a
chunk containing a MobSpawnerTile / ChestTile / etc, the null deref
crashes at addr 0x8 (instance's first field).

Two sites in Chunk.cpp (lines 409 and 747). Both need guarding.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "Chunk.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_TERD_NULL" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = "if (TileEntityRenderDispatcher::instance->hasRenderer(et))"
new = "if (TileEntityRenderDispatcher::instance && TileEntityRenderDispatcher::instance->hasRenderer(et)) /* MCLE_iOS_TERD_NULL */"

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

count = src.count(old)
src = src.replace(old, new)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET} ({count} sites)")
