#!/usr/bin/env python3
"""Diagnostic: force skyList to draw bright red.

Replaces the glColor3f(sr, sg, sb) call right before glCallList(skyList)
with glColor3f(1, 0, 0). If the player's reported "blue layer that
covers the orange horizon at sunset" is the sky-dome plane showing
through chunk gaps, it'll turn pure red after this patch lands.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_DEBUG_SKYLIST_RED" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = "\tglEnable(GL_FOG);\n\tglColor3f(sr, sg, sb);\n\tglCallList(skyList);"
new = (
    "\tglEnable(GL_FOG);\n"
    "\tglColor3f(1.0f, 0.0f, 0.0f); /* MCLE_iOS_DEBUG_SKYLIST_RED */\n"
    "\tglCallList(skyList);"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
