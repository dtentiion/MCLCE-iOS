#!/usr/bin/env python3
"""The block drawn over the screen inside a wall is the one at the eyes.

ItemInHandRenderer::renderScreenEffect takes the block at the player's y
for the texture it covers the screen with. On the client that y is at eye
height. The player here is the server's, whose y is at its feet, so with
its head in a block the one looked up was the air it stands in and nothing
was drawn. On iOS the block is taken at the y the client's player would
have, like patch-iteminhand-light-eye.py does for the hand's light.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "ItemInHandRenderer.cpp"

OLD = "        int y = Mth::floor(minecraft->player->y);\n"
NEW = ("#ifdef __APPLE_IOS__\n"
       "        // MCLE_INWALL_Y: the client player's y, heightOffset above the feet\n"
       "        int y = Mth::floor(minecraft->player->y + 1.62 - minecraft->player->heightOffset);\n"
       "#else\n"
       "        int y = Mth::floor(minecraft->player->y);\n"
       "#endif\n")

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
src = SRC.read_bytes().decode("utf-8", errors="replace")
nl = "\r\n" if "\r\n" in src else "\n"
if "MCLE_INWALL_Y" in src:
    print(f"already patched: {SRC}")
    sys.exit(0)
old = OLD.replace("\n", nl)
if src.count(old) != 1:
    sys.exit(f"anchor not found once in {SRC}")
src = src.replace(old, NEW.replace("\n", nl))
SRC.write_text(src, encoding="utf-8", errors="replace", newline="")
print(f"patched: {SRC}")
