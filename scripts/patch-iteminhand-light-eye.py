#!/usr/bin/env python3
"""The hand takes its light where the client's player is, at eye height.

ItemInHandRenderer::render lights the hand from the block at the player's
y. On the client that's the LocalPlayer, whose y sits heightOffset (1.62)
above its feet. Here the player is the server's own ServerPlayer, and
ServerPlayer's constructor sets heightOffset to 0, so its y is at its feet
and the hand was lit from the block at its feet instead. On iOS the light
is read at the y the client's player would have.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "ItemInHandRenderer.cpp"

OLDS = [
    "    float br = minecraft->level->getBrightness(Mth::floor(player->x), Mth::floor(player->y), Mth::floor(player->z));\n",
    "        int col = minecraft->level->getLightColor(Mth::floor(player->x), Mth::floor(player->y), Mth::floor(player->z), 0);\n",
]

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
src = SRC.read_bytes().decode("utf-8", errors="replace")
nl = "\r\n" if "\r\n" in src else "\n"
if "MCLE_HAND_LIGHT_Y" in src:
    print(f"already patched: {SRC}")
    sys.exit(0)
for old in OLDS:
    o = old.replace("\n", nl)
    if src.count(o) != 1:
        sys.exit(f"anchor not found once in {SRC}")
    new = old.replace("Mth::floor(player->y)", "Mth::floor(MCLE_HAND_LIGHT_Y)")
    src = src.replace(o, new.replace("\n", nl))

DEFINE = ("#ifdef __APPLE_IOS__\n"
          "// The client player's y, heightOffset above the feet (ServerPlayer's is 0)\n"
          "#define MCLE_HAND_LIGHT_Y (player->y + 1.62 - player->heightOffset)\n"
          "#else\n"
          "#define MCLE_HAND_LIGHT_Y (player->y)\n"
          "#endif\n")
ANCHOR = '#include "ItemInHandRenderer.h"\n'.replace("\n", nl)
if src.count(ANCHOR) != 1:
    sys.exit(f"include anchor not found once in {SRC}")
src = src.replace(ANCHOR, ANCHOR + DEFINE.replace("\n", nl))
SRC.write_text(src, encoding="utf-8", errors="replace", newline="")
print(f"patched: {SRC}")
