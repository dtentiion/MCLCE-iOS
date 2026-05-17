#!/usr/bin/env python3
"""Diagnostic: paint the moon quad bright magenta.

Inserts a glColor4f(1, 0, 1, 1) right before the moon quad emit in
renderSky. If the player's reported 'blue rectangle on opposite side
from sun' is the moon quad, it'll turn pure magenta after this lands.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_DEBUG_MOON_MAGENTA" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# Anchor is the bindTexture call right before the moon quad emit.
old = (
    "\t\tss = 20;\n"
    "\t\ttextures->bindTexture(&MOON_PHASES_LOCATION); // 4J was L\"/1_2_2/terrain/moon_phases.png\""
)
new = (
    "\t\tss = 20;\n"
    "\t\tglColor4f(1.0f, 0.0f, 1.0f, 1.0f); /* MCLE_iOS_DEBUG_MOON_MAGENTA */\n"
    "\t\ttextures->bindTexture(&MOON_PHASES_LOCATION); // 4J was L\"/1_2_2/terrain/moon_phases.png\""
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
