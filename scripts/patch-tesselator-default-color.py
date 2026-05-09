#!/usr/bin/env python3
"""Patch upstream Tesselator.cpp to ALWAYS write a per-vertex color.

When upstream emits vertices via t->vertex() without a preceding
t->color(), `hasColor` stays false and the color slot in the vertex data
isn't written - line 929-932 only writes it when hasColor=true. On real
GL hardware that's fine: glDisableClientState(GL_COLOR_ARRAY) means the
pipeline uses the global glColor4f for those vertices.

Our Metal pipeline always reads the color attribute from the vertex
buffer. With the slot uninitialised, the vertex shader gets garbage (or
zero, killing output via i.color*t = 0). This is exactly what makes the
sky dome (skyList builds via t->vertex() only - LevelRenderer.cpp:184-200)
contribute nothing to the framebuffer, leaving the user-visible sky as
just the clear-color background instead of the time-modulated dome.

Fix: write 0xffffffff (white) when hasColor=false. Then per-vertex color
multiplied by current uniform color (set via glColor3f/4f) equals the
uniform color - parity with how real GL fixed-function pipelines behave.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "Tesselator.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_DEFAULT_COLOR" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# Replace the conditional color write with an unconditional one that
# falls back to white when hasColor=false.
old = (
    "\t\tif (hasColor)\n"
    "\t\t{\n"
    "\t\t\t_array->data[p + 5] = col;\n"
    "\t\t}"
)
new = (
    "\t\t// MCLE_iOS_DEFAULT_COLOR: always write color so Metal vertex shader\n"
    "\t\t// reads a defined value. White when hasColor=false - parity with\n"
    "\t\t// real GL using global glColor4f when GL_COLOR_ARRAY is disabled.\n"
    "\t\t_array->data[p + 5] = hasColor ? col : (int)0xffffffff;"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
