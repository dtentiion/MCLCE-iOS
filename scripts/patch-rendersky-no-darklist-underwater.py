#!/usr/bin/env python3
"""Diagnostic: also skip the underwater/cave darkList draw in renderSky.

We previously disabled the always-on overworld darkList (skipped via
patch-rendersky-no-darklist-overworld.py). A second darkList draw
still exists earlier in renderSky, gated on yy < 0 (player below the
horizon height of 63). The player describes a 'blue carpet that
rises briefly at sunset/sunrise on the opposite side from the sun' -
which matches darkList's geometry (32x32 flat grid at world y=-16
translated up by 12, colored black via the preceding glColor3f(0,0,0)
but reading as dark blue under fog at sunset).

If yy briefly drops below 63 due to alpha interpolation around chunk
boundaries this gate fires and renders the carpet. Skip it as a probe.
If the carpet disappears, confirmed. Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_SKIP_DARKLIST_UNDERWATER" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "\tif (yy < 0)\n"
    "\t{\n"
    "\t\tglPushMatrix();\n"
    "\t\tglTranslatef(0, -static_cast<float>(-12), 0);\n"
    "\t\tglCallList(darkList);\n"
    "\t\tglPopMatrix();"
)
new = (
    "\tif (false && yy < 0) /* MCLE_iOS_SKIP_DARKLIST_UNDERWATER probe */\n"
    "\t{\n"
    "\t\tglPushMatrix();\n"
    "\t\tglTranslatef(0, -static_cast<float>(-12), 0);\n"
    "\t\tglCallList(darkList);\n"
    "\t\tglPopMatrix();"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
