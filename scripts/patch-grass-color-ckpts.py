#!/usr/bin/env python3
"""bracket GrassTile::getColor to find where addr 0xe0 crashes for grass"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.World" / "GrassTile.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "GTC_CKPT" in src:
    print("already patched"); sys.exit(0)

# Bracket GrassTile::getColor(level, x, y, z)
old = (
    "int GrassTile::getColor(LevelSource *level, int x, int y, int z)\n"
    "{\n"
    "\treturn getColor( level, x, y, z, level->getData( x, y, z ) );\n"
    "}"
)
new = (
    "int GrassTile::getColor(LevelSource *level, int x, int y, int z)\n"
    "{\n"
    "\tapp.DebugPrintf(\"GTC_CKPT 3-arg entry level=%p xyz=%d,%d,%d\", level, x, y, z);\n"
    "\tif (!level) { app.DebugPrintf(\"GTC_CKPT level null - return 0xffffff\"); return 0xffffff; }\n"
    "\treturn getColor( level, x, y, z, level->getData( x, y, z ) );\n"
    "}"
)
if old not in src: sys.exit("3-arg entry anchor not found")
src = src.replace(old, new, 1)

# Bracket the for loop in 4-arg getColor
old2 = (
    "\tfor (int oz = -1; oz <= 1; oz++)\n"
    "\t{\n"
    "\t\tfor (int ox = -1; ox <= 1; ox++)\n"
    "\t\t{\n"
    "\t\t\tint grassColor = level->getBiome(x + ox, z + oz)->getGrassColor();"
)
new2 = (
    "\tapp.DebugPrintf(\"GTC_CKPT 4-arg entry level=%p xyz=%d,%d,%d data=%d\", level, x, y, z, data);\n"
    "\tfor (int oz = -1; oz <= 1; oz++)\n"
    "\t{\n"
    "\t\tfor (int ox = -1; ox <= 1; ox++)\n"
    "\t\t{\n"
    "\t\t\tapp.DebugPrintf(\"GTC_CKPT before getBiome ox=%d oz=%d\", ox, oz);\n"
    "\t\t\tBiome *biome = level->getBiome(x + ox, z + oz);\n"
    "\t\t\tapp.DebugPrintf(\"GTC_CKPT biome=%p\", biome);\n"
    "\t\t\tif (biome == nullptr) { app.DebugPrintf(\"GTC_CKPT biome null - skip\"); continue; }\n"
    "\t\t\tapp.DebugPrintf(\"GTC_CKPT before getGrassColor\");\n"
    "\t\t\tint grassColor = biome->getGrassColor();"
)
if old2 not in src: sys.exit("4-arg loop anchor not found")
src = src.replace(old2, new2, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
