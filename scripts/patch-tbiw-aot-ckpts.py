#!/usr/bin/env python3
"""Null-guard inside TileRenderer::tesselateBlockInWorld...AmbienceOcclusion.

Originally bracketed every step with TBIW_CKPT log lines while pinning
an addr 0xe0 crash. The crash is fixed; per-tile logging dominated
the log file and stalls workers. Keep only the getTexture null guard.
"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "TileRenderer.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_TBIW_GUARDS" in src:
    print("already patched"); sys.exit(0)

old = "\t\tif ( getTexture(tt)->getFlags() == Icon::IS_GRASS_TOP ) tintSides = false;"
new = (
    "\t\tIcon *_tbiwTex = getTexture(tt); // MCLE_TBIW_GUARDS\n"
    "\t\tif (_tbiwTex == nullptr) return false;\n"
    "\t\tif ( _tbiwTex->getFlags() == Icon::IS_GRASS_TOP ) tintSides = false;"
)
if old not in src: sys.exit("getFlags anchor not found")
src = src.replace(old, new, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
