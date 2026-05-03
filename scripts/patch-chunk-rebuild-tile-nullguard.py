#!/usr/bin/env python3
"""G5-step20: null-guard Tile::tiles[tileId] in Chunk::rebuild's mesh
loop. Some procgen tileIds map to null Tile entries (not all block IDs
are populated). The virtual call tile->isEntityTile() then crashes at
addr 0x0.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "Chunk.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "TILE_NULLGUARD" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "\t\t\t\t\t\tTile *tile = Tile::tiles[tileId];\n"
    "\t\t\t\t\t\tif (currentLayer == 0 && tile->isEntityTile())"
)
new = (
    "\t\t\t\t\t\tTile *tile = Tile::tiles[tileId];\n"
    "\t\t\t\t\t\tif (!tile) continue; // TILE_NULLGUARD: skip block ids without registered Tile\n"
    "\t\t\t\t\t\tif (currentLayer == 0 && tile->isEntityTile())"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")
src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
