#!/usr/bin/env python3
"""A texture pack can be stitched before there is a level renderer.

Minecraft::init makes the level renderer at startup, so the stitch can
always hand it the breaking textures. The port makes it with the first
world, and a texture pack picked in the Load menu reloads the textures
(eAppAction_ReloadTexturePack) before that. On iOS the stitch skips the
level renderer when there isn't one yet; the first world's own stitch,
made right after the renderer, registers them.

Runs after patch-stitch-checkpoints.py. Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FILES = [
    REPO / "upstream" / "Minecraft.Client" / "PreStitchedTextureMap.cpp",
    REPO / "upstream" / "Minecraft.Client" / "TextureMap.cpp",
]

OLD = "\t\tMinecraft::GetInstance()->levelRenderer->registerTextures(this);\n"
NEW = ("#ifdef __APPLE_IOS__\n"
       "\t\t// No level renderer before the first world\n"
       "\t\tif (Minecraft::GetInstance()->levelRenderer != nullptr)\n"
       "#endif\n" + OLD)

for path in FILES:
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_bytes().decode("utf-8", errors="replace")
    nl = "\r\n" if "\r\n" in src else "\n"
    old = OLD.replace("\n", nl)
    new = NEW.replace("\n", nl)
    if new in src:
        print(f"already patched: {path}")
        continue
    if src.count(old) != 1:
        sys.exit(f"anchor not found once in {path}")
    path.write_text(src.replace(old, new), encoding="utf-8", errors="replace", newline="")
    print(f"patched: {path}")
