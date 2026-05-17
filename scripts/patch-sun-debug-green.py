#!/usr/bin/env python3
"""Diagnostic: paint the sun quad bright green.

Inserts glColor4f(0,1,0,1) right before the sun quad emit in renderSky.
Combined with the moon-magenta patch, triangulates which celestial
quad (if any) is the 'flat blue carpet'.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_DEBUG_SUN_GREEN" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "\t\tMemSect(31);\n"
    "\t\ttextures->bindTexture(&SUN_LOCATION);"
)
new = (
    "\t\tglColor4f(0.0f, 1.0f, 0.0f, 1.0f); /* MCLE_iOS_DEBUG_SUN_GREEN */\n"
    "\t\tMemSect(31);\n"
    "\t\ttextures->bindTexture(&SUN_LOCATION);"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
