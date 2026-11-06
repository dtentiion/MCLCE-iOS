#!/usr/bin/env python3
"""A jump costs the local player what it costs on TU19.

There's no LocalPlayer on iOS, the local player is the ServerPlayer and
jumps itself. On TU19 the client's player does the jumping, and its
Player::jumpFromGround exhaustion does nothing on a client level. The
server charges a plain EXHAUSTION_JUMP instead, when
PlayerConnection::handleMovePlayer sees the player leave the ground
going up, sprinting or not.

Player::jumpFromGround skips its exhaustion for the ServerPlayer, which
here is always the local player, and Game/MCLEGameLoop.cpp does
handleMovePlayer's charge.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.World" / "Player.cpp"
MARKER = "charges the jump like handleMovePlayer"

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
src = SRC.read_text(encoding="utf-8", errors="replace")
if MARKER in src:
    print(f"already patched: {SRC}")
    sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"
OLD = ("\t//awardStat(Stats::jump, 1);\n"
       "\n"
       "\tif (isSprinting())\n"
       "\t{\n"
       "\t\tcauseFoodExhaustion(FoodConstants::EXHAUSTION_SPRINT_JUMP);\n").replace("\n", nl)
NEW = ("\t//awardStat(Stats::jump, 1);\n"
       "\n"
       "#ifdef __APPLE_IOS__\n"
       "\t// The local player jumps like the client's, where this costs nothing.\n"
       "\t// Game/MCLEGameLoop.cpp charges the jump like handleMovePlayer.\n"
       "\tif (GetType() == eTYPE_SERVERPLAYER) return;\n"
       "#endif\n"
       "\tif (isSprinting())\n"
       "\t{\n"
       "\t\tcauseFoodExhaustion(FoodConstants::EXHAUSTION_SPRINT_JUMP);\n").replace("\n", nl)
if src.count(OLD) != 1:
    sys.exit(f"anchor not found once in {SRC}")
SRC.write_text(src.replace(OLD, NEW), encoding="utf-8", errors="replace", newline="")
print(f"patched: {SRC}")
