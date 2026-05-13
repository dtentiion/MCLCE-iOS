#!/usr/bin/env python3
"""One-shot log inside the grass-side overlay second-pass at
TileRenderer.cpp:5599 so we can see what pBase values are actually
reaching the overlay multiply.

Goal: pin whether the biome tint pBaseRed/Green/Blue is non-1.0 when
the overlay pass fires. If pBase is (1,1,1) the multiply is a no-op
and the overlay renders with default vertex colors regardless of
biome - matching the symptom of a flat-green grass side.

Logs once via static counter to avoid flooding.
Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "TileRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_GRASS_OVERLAY_LOG" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# Anchor: the first occurrence of the overlay second-pass conditional.
old = (
    "\t\tif ( fancy && (tex->getFlags() == Icon::IS_GRASS_SIDE) && !hasFixedTexture() )\n"
    "\t\t{\n"
    "\t\t\tc1r *= pBaseRed;\n"
    "\t\t\tc2r *= pBaseRed;\n"
    "\t\t\tc3r *= pBaseRed;\n"
    "\t\t\tc4r *= pBaseRed;"
)
new = (
    "\t\tif ( fancy && (tex->getFlags() == Icon::IS_GRASS_SIDE) && !hasFixedTexture() )\n"
    "\t\t{\n"
    "\t\t\t// MCLE_iOS_GRASS_OVERLAY_LOG\n"
    "\t\t\t{ extern int mcle_log_msg(const char *); static int s_n=0; if(s_n<3){ char b[160]; snprintf(b,sizeof(b),\"GRASS_OVERLAY_HIT pBase=(%.3f,%.3f,%.3f) c1=(%.3f,%.3f,%.3f)\", pBaseRed,pBaseGreen,pBaseBlue,c1r,c1g,c1b); mcle_log_msg(b); s_n++; } }\n"
    "\t\t\tc1r *= pBaseRed;\n"
    "\t\t\tc2r *= pBaseRed;\n"
    "\t\t\tc3r *= pBaseRed;\n"
    "\t\t\tc4r *= pBaseRed;"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
