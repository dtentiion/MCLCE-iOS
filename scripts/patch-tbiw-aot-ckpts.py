#!/usr/bin/env python3
"""bracket tesselateBlockInWorldWithAmbienceOcclusionTexLighting to find addr 0xe0 crash"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "TileRenderer.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "TBIW_CKPT" in src:
    print("already patched"); sys.exit(0)

# Entry: replace the very first line of the function body (the Icon *uniformTex... line)
# by injecting a CKPT before it. That line is unique inside this function.
old = "\tIcon *uniformTex = nullptr;\n\tint id = tt->id;"
new = (
    "\tapp.DebugPrintf(\"TBIW_CKPT enter this=%p tt=%p xyz=%d,%d,%d level=%p\", this, tt, pX, pY, pZ, level);\n"
    "\tIcon *uniformTex = nullptr;\n"
    "\tint id = tt->id;\n"
    "\tapp.DebugPrintf(\"TBIW_CKPT after id=%d\", id);"
)
if src.count(old) != 1: sys.exit(f"entry anchor count={src.count(old)}")
src = src.replace(old, new, 1)

# bracket the getTexture(tt)->getFlags() call AND inspect tt's vtable pointer
old2 = "\t\tif ( getTexture(tt)->getFlags() == Icon::IS_GRASS_TOP ) tintSides = false;"
new2 = (
    "\t\t{\n"
    "\t\t\tvoid *_vt = tt ? *(void**)tt : (void*)0xDEAD;\n"
    "\t\t\tapp.DebugPrintf(\"TBIW_CKPT inspect tt=%p vptr=%p id=%d\", tt, _vt, tt?tt->id:-1);\n"
    "\t\t}\n"
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
