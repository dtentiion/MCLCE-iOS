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
    "\t// crashing when icon registration hasn't caught up yet. Log\n"
    "\t// the first few null hits so we can see which path triggers.\n"
    "\tif (icon == nullptr) {\n"
    "\t\textern int mcle_log_msg(const char *);\n"
    "\t\tstatic int s_nullCount = 0;\n"
    "\t\tif (s_nullCount < 16) {\n"
    "\t\t\tchar buf[160];\n"
    "\t\t\tsnprintf(buf, sizeof(buf),\n"
    "\t\t\t\t\"NULL_ICON_GUARD count=%d mc=%p tex=%p\",\n"
    "\t\t\t\ts_nullCount,\n"
    "\t\t\t\t(void*)minecraft,\n"
    "\t\t\t\tminecraft ? (void*)minecraft->textures : (void*)0);\n"
    "\t\t\tmcle_log_msg(buf);\n"
    "\t\t\ts_nullCount++;\n"
    "\t\t}\n"
    "\t\tif (!minecraft || !minecraft->textures) return nullptr;\n"
    "\t\treturn minecraft->textures->getMissingIcon(Icon::TYPE_TERRAIN);\n"
    "\t}"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
