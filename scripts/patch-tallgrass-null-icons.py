#!/usr/bin/env python3
"""Defensive guard for uninitialized TallGrass::icons.

The crash hits TallGrass::getTexture when icons is still a null /
garbage pointer left over from an uninitialized heap slot. The proper
init path calls registerIcons during TextureMap::stitch but the field
itself is not zero-initialized at construction, so any code path that
reaches getTexture before stitch (or before icons gets a valid
pointer for any reason) crashes with EXC_BAD_ACCESS at 0x8.

Two-part fix:
- zero icons in the TallGrass constructor so the field always starts
  as a known nullptr
- return nullptr from getTexture when icons is still null. TileRenderer
  handles a null icon by skipping the face draw; visually we lose
  tall grass briefly until registerIcons runs, instead of crashing.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.World" / "TallGrass.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_TALLGRASS_NULL_GUARD" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old_ctor = (
    "TallGrass::TallGrass(int id) : Bush(id, Material::replaceable_plant)\n"
    "{\n"
    "\tthis->updateDefaultShape();\n"
    "}"
)
new_ctor = (
    "TallGrass::TallGrass(int id) : Bush(id, Material::replaceable_plant)\n"
    "{\n"
    "\t// MCLE_iOS_TALLGRASS_NULL_GUARD: zero icons so any read before\n"
    "\t// registerIcons runs returns a known nullptr instead of garbage.\n"
    "\ticons = nullptr;\n"
    "\tthis->updateDefaultShape();\n"
    "}"
)

old_gt = (
    "Icon *TallGrass::getTexture(int face, int data)\n"
    "{\n"
    "\tif (data >= TALL_GRASS_TILE_NAMES_LENGTH) data = 0;\n"
    "\treturn icons[data];\n"
    "}"
)
new_gt = (
    "Icon *TallGrass::getTexture(int face, int data)\n"
    "{\n"
    "\tif (data >= TALL_GRASS_TILE_NAMES_LENGTH) data = 0;\n"
    "\tif (!icons) return nullptr; // MCLE_iOS_TALLGRASS_NULL_GUARD\n"
    "\treturn icons[data];\n"
    "}"
)

if old_ctor not in src:
    sys.exit(f"ctor anchor not found in {TARGET}")
if old_gt not in src:
    sys.exit(f"getTexture anchor not found in {TARGET}")

src = src.replace(old_ctor, new_ctor, 1)
src = src.replace(old_gt, new_gt, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
