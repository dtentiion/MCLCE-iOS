#!/usr/bin/env python3
"""Pull the in-game tooltip code out of upstream Minecraft::tick.

Minecraft::tick works out the A / X / Y / LT / RT... tooltips from what the
player is looking at and holding, then calls ui.SetTooltips. Minecraft.cpp
doesn't build on iOS, so that block is copied into
upstream/Minecraft.Client/iOS_IngameTooltips.inc and Game/MCLETooltips.cpp
compiles it with the names it expects (player, level, hitResult, gameMode,
ui, InputManager).

The block asks level->isClientSide, which is always true for the client
level it was written for. Here the level is the server one, so that becomes
a constant.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Minecraft.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_IngameTooltips.inc"

START = "// 4J-PB - add some tooltips if required"
END = "ui.SetTooltips( iPad, iA, iB, iX, iY, iLT, iRT, iLB, iRB, iLS, iRS);"

src = SRC.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
if src.count(START) != 1 or src.count(END) != 1:
    print(f"tooltip block markers not found once in {SRC}")
    sys.exit(1)
begin = src.index(START)
end = src.index(END) + len(END)
block = src[begin:end]
block = block.replace("level->isClientSide", "levelIsClientSide")
OUT.write_text(block + "\n", encoding="utf-8")
print(f"wrote {OUT} ({block.count(chr(10)) + 1} lines)")
