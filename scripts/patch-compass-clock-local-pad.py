#!/usr/bin/env python3
"""Use pad 0 for the compass and clock icons.

CompassItem::getIcon and ClockItem::getIcon take the icon for the local
player's pad out of icons[XUSER_MAX_COUNT]. mc->player is the ServerPlayer
aliased as a MultiplayerLocalPlayer, so GetXboxPad reads whatever
ServerPlayer has at LocalPlayer's m_iPad offset and the icon is read from
past the end of the array. There's one local player on pad 0, the one
compassP0 and clockP0 follow.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORLD = REPO / "upstream" / "Minecraft.World"

OLD = "icon = icons[pMinecraft->player->GetXboxPad()];"
NEW = "icon = icons[0];"

for name in ("CompassItem.cpp", "ClockItem.cpp"):
    path = WORLD / name
    src = path.read_bytes().decode("utf-8", errors="replace")
    count = src.count(OLD)
    if count == 0:
        if NEW not in src:
            print(f"icon lookup not found in {path}")
            sys.exit(1)
        print(f"already patched: {path}")
        continue
    if count != 1:
        print(f"expected 1 icon lookup in {path}, found {count}")
        sys.exit(1)
    path.write_bytes(src.replace(OLD, NEW).encode("utf-8"))
    print(f"patched: {path}")
