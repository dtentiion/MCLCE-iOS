#!/usr/bin/env python3
"""Null-guards inside GrassTile::getColor.

Originally bracketed each step with per-tile GTC_CKPT logs to pin the
addr 0xe0 crash. The crash is fixed; the per-tile logs were firing
~45 lines per grass tile rebuild and dominated the log file. Keeping
the structural null guards (level / biome) only.
"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.World" / "GrassTile.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_GTC_GUARDS" in src:
    print("already patched"); sys.exit(0)

old = (
    "int GrassTile::getColor(LevelSource *level, int x, int y, int z)\n"
    "{\n"
    "\treturn getColor( level, x, y, z, level->getData( x, y, z ) );\n"
    "}"
)
new = (
    "int GrassTile::getColor(LevelSource *level, int x, int y, int z)\n"
    "{\n"
    "\tif (!level) return 0xffffff; // MCLE_GTC_GUARDS\n"
    "\treturn getColor( level, x, y, z, level->getData( x, y, z ) );\n"
    "}"
)
if old not in src: sys.exit("3-arg entry anchor not found")
src = src.replace(old, new, 1)

old2 = (
    "\tfor (int oz = -1; oz <= 1; oz++)\n"
    "\t{\n"
    "\t\tfor (int ox = -1; ox <= 1; ox++)\n"
    "\t\t{\n"
    "\t\t\tint grassColor = level->getBiome(x + ox, z + oz)->getGrassColor();"
)
new2 = (
    "\tfor (int oz = -1; oz <= 1; oz++)\n"
    "\t{\n"
    "\t\tfor (int ox = -1; ox <= 1; ox++)\n"
    "\t\t{\n"
    "\t\t\tBiome *biome = level->getBiome(x + ox, z + oz);\n"
    "\t\t\tif (biome == nullptr) continue;\n"
    "\t\t\tint grassColor = biome->getGrassColor();"
)
if old2 not in src: sys.exit("4-arg loop anchor not found")
src = src.replace(old2, new2, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
