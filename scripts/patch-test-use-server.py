#!/usr/bin/env python3
"""The tooltip tests leave the game alone when they run on the server's player.

The tooltips ask each tile and item whether using it would do anything,
with the bTestUse flags. On the consoles that is asked of the client's
player on the client's level, where two paths have nothing to guard:
ServerPlayer::startSleepInBed sends the sleep packet and teleports the
player without looking at bTestUse (only LocalPlayer's is ever tested),
and DyePowderItem::useOn fires the bone meal particles on any level that
isn't the client's. Here the tests run on the server's player and level,
so looking at a bed stopped the player's movement every tick and holding
bone meal over something it can grow showered particles. Both now look at
the flag.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UP = REPO / "upstream"

EDITS = [
    (UP / "Minecraft.Client" / "ServerPlayer.cpp",
     "\tBedSleepingResult result = Player::startSleepInBed(x, y, z, bTestUse);\n"
     "\tif (result == OK)\n",
     "\tBedSleepingResult result = Player::startSleepInBed(x, y, z, bTestUse);\n"
     "\tif (result == OK && !bTestUse)\n"),
    (UP / "Minecraft.World" / "DyePowderItem.cpp",
     "\t\t\tif (!level->isClientSide) level->levelEvent(LevelEvent::PARTICLES_PLANT_GROWTH, x, y, z, 0);\n",
     "\t\t\tif (!level->isClientSide && !bTestUseOnOnly) level->levelEvent(LevelEvent::PARTICLES_PLANT_GROWTH, x, y, z, 0);\n"),
]

for path, old, new in EDITS:
    if not path.exists():
        sys.exit(f"missing: {path}")
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        src = f.read()
    nl = "\r\n" if "\r\n" in src else "\n"
    old = old.replace("\n", nl)
    new = new.replace("\n", nl)
    if new in src:
        print(f"already patched: {path}")
        continue
    if src.count(old) != 1:
        sys.exit(f"anchor not found once in {path}")
    with open(path, "w", encoding="utf-8", errors="replace", newline="") as f:
        f.write(src.replace(old, new))
    print(f"patched: {path}")
