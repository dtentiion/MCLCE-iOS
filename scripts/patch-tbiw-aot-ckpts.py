#!/usr/bin/env python3
"""bracket tesselateBlockInWorldWithAmbienceOcclusionTexLighting to find addr 0xe0 crash"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "TileRenderer.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "TBIW_CKPT" in src:
    print("already patched"); sys.exit(0)

# Entry of tesselateBlockInWorldWithAmbienceOcclusionTexLighting
old = (
    "bool TileRenderer::tesselateBlockInWorldWithAmbienceOcclusionTexLighting( Tile* tt, int pX, int pY, int pZ,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t float pBaseRed, float pBaseGreen,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t float pBaseBlue, int faceFlags, bool smoothShapeLighting )\n"
    "{\n"
)
new = (
    "bool TileRenderer::tesselateBlockInWorldWithAmbienceOcclusionTexLighting( Tile* tt, int pX, int pY, int pZ,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t float pBaseRed, float pBaseGreen,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t float pBaseBlue, int faceFlags, bool smoothShapeLighting )\n"
    "{\n"
    "\tapp.DebugPrintf(\"TBIW_CKPT enter this=%p tt=%p id=%d xyz=%d,%d,%d level=%p\", this, tt, tt?tt->id:-1, pX, pY, pZ, level);\n"
)
if old not in src: sys.exit("entry anchor not found")
src = src.replace(old, new, 1)

# bracket the getTexture(tt)->getFlags() call
old2 = "\t\tif ( getTexture(tt)->getFlags() == Icon::IS_GRASS_TOP ) tintSides = false;"
new2 = (
    "\t\tapp.DebugPrintf(\"TBIW_CKPT before getTexture(tt) tt=%p\", tt);\n"
    "\t\tIcon *_tbiwTex = getTexture(tt);\n"
    "\t\tapp.DebugPrintf(\"TBIW_CKPT getTexture returned %p\", _tbiwTex);\n"
    "\t\tif (_tbiwTex == nullptr) { app.DebugPrintf(\"TBIW_CKPT bail: getTexture null\"); return false; }\n"
    "\t\tapp.DebugPrintf(\"TBIW_CKPT before getFlags\");\n"
    "\t\tif ( _tbiwTex->getFlags() == Icon::IS_GRASS_TOP ) tintSides = false;\n"
    "\t\tapp.DebugPrintf(\"TBIW_CKPT after getFlags\");"
)
if old2 not in src: sys.exit("getFlags anchor not found")
src = src.replace(old2, new2, 1)

# bracket the centerColor line just before getFlags chain
old3 = "\tint\t\t\tcenterColor = getLightColor(tt,  level, pX, pY, pZ );"
new3 = (
    "\tapp.DebugPrintf(\"TBIW_CKPT before getLightColor level=%p\", level);\n"
    "\tint\t\t\tcenterColor = getLightColor(tt,  level, pX, pY, pZ );\n"
    "\tapp.DebugPrintf(\"TBIW_CKPT after getLightColor color=%d\", centerColor);"
)
if old3 not in src: sys.exit("getLightColor anchor not found")
src = src.replace(old3, new3, 1)

# bracket the applyAmbienceOcclusion = true line - might be the crash
old4 = "\tapplyAmbienceOcclusion = true;"
new4 = (
    "\tapp.DebugPrintf(\"TBIW_CKPT before applyAO=true this=%p\", this);\n"
    "\tapplyAmbienceOcclusion = true;\n"
    "\tapp.DebugPrintf(\"TBIW_CKPT after applyAO=true\");"
)
if old4 not in src: sys.exit("applyAO anchor not found")
src = src.replace(old4, new4, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
