#!/usr/bin/env python3
"""Guard TileRenderer's getMissingIcon fallback against null minecraft / textures.

When a tile's getTexture returns nullptr (any Tile subclass that
hasn't had registerIcons called yet, or whose icons array is in an
indeterminate state), TileRenderer falls back to:

    minecraft->textures->getMissingIcon(Icon::TYPE_TERRAIN);

If minecraft is null OR minecraft->textures is null at that point,
the call crashes inside Textures::getMissingIcon with EXC_BAD_ACCESS
at the textures-instance member offset.

This patch guards the fallback so the renderer returns nullptr
instead of crashing - upstream TileRenderer code already handles
a null icon by skipping the face draw. Worst case: a tile renders
without its texture briefly; best case: the icon registration
catches up on the next frame and the tile draws normally.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "TileRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_TILERENDERER_NULL_GUARD" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "Icon *TileRenderer::getTextureOrMissing(Icon *icon)\n"
    "{\n"
    "\tif (icon == nullptr) return minecraft->textures->getMissingIcon(Icon::TYPE_TERRAIN);"
)
new = (
    "Icon *TileRenderer::getTextureOrMissing(Icon *icon)\n"
    "{\n"
    "\t// MCLE_iOS_TILERENDERER_NULL_GUARD: skip the face instead of\n"
    "\t// crashing when icon registration hasn't caught up yet.\n"
    "\tif (icon == nullptr) {\n"
    "\t\tif (!minecraft || !minecraft->textures) return nullptr;\n"
    "\t\treturn minecraft->textures->getMissingIcon(Icon::TYPE_TERRAIN);\n"
    "\t}"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
