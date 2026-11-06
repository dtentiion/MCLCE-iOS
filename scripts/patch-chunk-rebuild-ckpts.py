#!/usr/bin/env python3
"""Chunk::rebuild null guards: a chunk that isn't loaded and a tile id with no Tile."""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "Chunk.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "crbLc" in src:
    print("already patched"); sys.exit(0)

old = "\tlevel->getChunkAt(x,z)->getBlockData(tileArray);"
new = (
    "\tLevelChunk *crbLc = level->getChunkAt(x, z);\n"
    "\tif (crbLc) crbLc->getBlockData(tileArray);"
)
if old not in src: sys.exit("getChunkAt anchor not found")
src = src.replace(old, new, 1)

old = "\t\tTile *tile = Tile::tiles[tileId];"
new = (
    "\t\tTile *tile = Tile::tiles[tileId];\n"
    "\t\tif (tile == nullptr) continue;"
)
if old not in src: sys.exit("Tile::tiles anchor not found")
src = src.replace(old, new, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
