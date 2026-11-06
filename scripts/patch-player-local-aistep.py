#!/usr/bin/env python3
"""LocalPlayer::aiStep's flying, around the server player's Player::aiStep.

There's no LocalPlayer on iOS. The local player is the ServerPlayer, and
Game/MCLEPlayerInput.cpp does the part of LocalPlayer::aiStep that comes
before its Player::aiStep call. The part after it (the flying move, which
zeroes yd and keeps the player on the ground) has to run in the same place,
so Player::aiStep ends with a call into MCLEPlayerInput.cpp.

LocalPlayer::aiStep also clears noJumpDelay while flying. It's protected in
LivingEntity, so it's made public.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORLD = REPO / "upstream" / "Minecraft.World"
PLAYER = WORLD / "Player.cpp"
LIVING = WORLD / "LivingEntity.h"
HOOK = "mcle_player_ai_step_end"


def read(path):
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    return src, ("\r\n" if "\r\n" in src else "\n")


src, nl = read(PLAYER)
if HOOK in src:
    print(f"already patched: {PLAYER}")
else:
    head = "void Player::aiStep()" + nl + "{"
    nxt = nl + "void Player::touch("
    if src.count(head) != 1 or src.count(nxt) != 1:
        sys.exit(f"Player::aiStep not found once: {PLAYER}")
    start = src.index(head)
    close = src.rindex("}", start, src.index(nxt, start))
    src = (src[:start] +
           "#ifdef __APPLE_IOS__" + nl +
           "void " + HOOK + "(Player *player);" + nl +
           "#endif" + nl + nl +
           src[start:close] +
           "#ifdef __APPLE_IOS__" + nl +
           "\t" + HOOK + "(this);" + nl +
           "#endif" + nl +
           src[close:])
    PLAYER.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {PLAYER}")

src, nl = read(LIVING)
old = "protected:" + nl + "\tint noJumpDelay;"
new = "public:" + nl + "\tint noJumpDelay;"
if new in src:
    print(f"already patched: {LIVING}")
elif src.count(old) != 1:
    sys.exit(f"noJumpDelay not found once: {LIVING}")
else:
    LIVING.write_text(src.replace(old, new), encoding="utf-8", errors="replace", newline="")
    print(f"patched: {LIVING}")
