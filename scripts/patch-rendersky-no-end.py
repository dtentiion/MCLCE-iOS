#!/usr/bin/env python3
"""Disable the End-dimension sky-cube branch at the top of renderSky.

Upstream LevelRenderer::renderSky starts with:
    if (mc->level->dimension->id == 1)
    {
        ...
        textures->bindTexture(&END_SKY_LOCATION);
        ... draws 6 quads of a sky-cube around the player ...
    }

That sky cube is anchored to the camera at +/-100 in each axis with a
cloud-like tunnel texture. If dimension->id ever lands on 1 in our
build (uninitialized memory, wrong ctor wiring, etc.), the cube stays
glued to the player and produces the textured 'wedge' visible across
the upper half of the screen on top of the overworld sky.

We're overworld-only for now, so the safest move is to neuter this
branch entirely.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_DISABLE_END_SKY" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = "\tif (mc->level->dimension->id == 1)\n\t{"
new = "\tif (false /* MCLE_iOS_DISABLE_END_SKY: overworld-only build */)\n\t{"

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
