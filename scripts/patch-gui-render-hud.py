#!/usr/bin/env python3
"""Flip Gui.cpp's #define RENDER_HUD from 0 to 1.

4J's console builds set RENDER_HUD to 0 because they render the in-game
HUD (hotbar, health, hunger, xp, crosshair) via the XUI/SWF stack via
IUIScene_HUD. We don't have that wired on iOS yet, so the native PC-
style code in Gui::render (lines ~269+) - guarded by `#if RENDER_HUD` -
gives us a working hotbar with the same blit-based draw path the PC
build uses.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "Gui.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_RENDER_HUD" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = "#define RENDER_HUD 0"
new = "#define RENDER_HUD 1 /* MCLE_iOS_RENDER_HUD: was 0 */"

if old not in src:
    sys.exit(f"anchor `{old}` not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
