#!/usr/bin/env python3
"""Skip the always-on darkList draw at the bottom of renderSky.

Upstream renderSky ends with an unconditional darkList draw colored
either dark-blue (overworld) or sky color (other dimensions). It's a
32x32 grid of quads anchored to the camera that's meant to give the
horizon a darker band when viewed through the visible chunk gap to
the sky.

With our GL_TEXTURE_2D disable now properly honored, that draw lands
as a solid dark-blue plane near eye height. Because chunks render
*after* renderSky in our build (via replay_all_lists), any unloaded
or rebuilding chunk fragment shows the dark-blue dome through it -
visible as blue patches that jitter as chunks come and go.

For overworld-only we don't need the horizon-darken effect badly
enough to fight chunk loading. Skip the second darkList call. The
first (cave/underwater) draw at line 1163-1164 is gated on yy<0
and stays in place for that path.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_SKIP_DARKLIST_OW" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "\tglPushMatrix();\n"
    "\tglTranslatef(0, -static_cast<float>(yy - 16), 0);\n"
    "\tglCallList(darkList);\n"
    "\tglPopMatrix();\n"
    "\tglEnable(GL_TEXTURE_2D);"
)
new = (
    "\t// MCLE_iOS_SKIP_DARKLIST_OW: chunks render after renderSky in our\n"
    "\t// build so any chunk gap shows this dome through it as blue\n"
    "\t// jittery patches. Skip the always-on horizon darken draw.\n"
    "\t#if 0\n"
    "\tglPushMatrix();\n"
    "\tglTranslatef(0, -static_cast<float>(yy - 16), 0);\n"
    "\tglCallList(darkList);\n"
    "\tglPopMatrix();\n"
    "\t#endif\n"
    "\tglEnable(GL_TEXTURE_2D);"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
