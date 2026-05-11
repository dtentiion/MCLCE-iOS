#!/usr/bin/env python3
"""Force upstream renderClouds to always take the simple 2D path.

renderClouds checks options->fancyGraphics and calls renderAdvancedClouds
if true. renderAdvancedClouds depends on glMultiTexCoord2f + viewport
clip planes we don't shim well, so we want the simple path regardless.

But fancyGraphics also controls Tile::leaves->setFancy(...) at
LevelRenderer.cpp:427. Fancy leaves use a cutout transparency that
matches what users expect in LCE. Fast leaves render as fully opaque
blocks.

We want both: fancy leaves (cutout transparency) AND simple cloud path.
The cleanest way is set fancyGraphics=true (turns on fancy leaves) and
patch renderClouds to ignore that setting for clouds.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_FORCE_SIMPLE_CLOUDS" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "\tif (mc->options->fancyGraphics)\n"
    "\t{\n"
    "\t\trenderAdvancedClouds(alpha);\n"
    "\t\treturn;\n"
    "\t}"
)
new = (
    "\t// MCLE_iOS_FORCE_SIMPLE_CLOUDS: skip renderAdvancedClouds (depends\n"
    "\t// on glMultiTexCoord2f + viewport clip planes not shimmed on iOS).\n"
    "\t// fancyGraphics stays true so leaves get cutout transparency at\n"
    "\t// LevelRenderer.cpp:427 setFancy().\n"
    "\t#if 0\n"
    "\tif (mc->options->fancyGraphics)\n"
    "\t{\n"
    "\t\trenderAdvancedClouds(alpha);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\t#endif"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
