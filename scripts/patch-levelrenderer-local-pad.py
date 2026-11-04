#!/usr/bin/env python3
"""Use pad 0 for every mc->player->GetXboxPad() in LevelRenderer.cpp.

mc->player is the ServerPlayer aliased as a MultiplayerLocalPlayer, so
GetXboxPad reads whatever ServerPlayer has at LocalPlayer's m_iPad offset.
The clamp patch only covers the `int playerIndex = ...` lines; setDirty
(every tileChanged), renderHitOutline and renderDestroyAnimation index
level[] with the raw value. There's one local player on pad 0.

Run after the other LevelRenderer patches. Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

src = TARGET.read_text(encoding="utf-8", errors="replace")
OLD = "mc->player->GetXboxPad()"
NEW = "0 /* MCLE_iOS_LOCAL_PAD */"
count = src.count(OLD)
if count == 0:
    if NEW in src:
        print(f"already patched: {TARGET}")
        sys.exit(0)
    sys.exit("no GetXboxPad calls found")
TARGET.write_text(src.replace(OLD, NEW), encoding="utf-8", errors="replace", newline="")
print(f"patched {count} sites: {TARGET}")
