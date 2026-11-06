#!/usr/bin/env python3
"""Pull IUIScene_AbstractContainerMenu's body out of upstream.

IUIScene_AbstractContainerMenu.cpp is the pointer, slot and tooltip logic
every container menu shares, but it includes the Iggy UI.h, so it can't be
built as it is. Its functions are copied into
upstream/Minecraft.Client/iOS_AbstractContainerMenu.inc and compiled by
Game/MCLEContainerMenu.cpp against small stand-ins for `ui` and the
Windows64 keyboard and mouse.

What changes:
- The local player here is the server player, so the menu goes on it, the
  carried item comes off it, and a slot click goes to its container the way
  PlayerConnection::handleContainerClick does (handleInventoryMouseClick
  would send a packet to a server that is this same player).
- Tutorial.cpp isn't built and Minecraft::localgameModes is never set, as on
  any world that isn't the tutorial, so the localgameModes checks go through
  mcle_container_game_mode(), which is always null.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "UI" / "IUIScene_AbstractContainerMenu.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_AbstractContainerMenu.inc"

START = "IUIScene_AbstractContainerMenu::IUIScene_AbstractContainerMenu()\n"

REPLACEMENTS = [
    ("\tMinecraft::GetInstance()->localplayers[iPad]->containerMenu = menu;\n",
     "\tmcle_container_player()->containerMenu = menu;\n", 1),
    ("\tshared_ptr<LocalPlayer> player = Minecraft::GetInstance()->localplayers[getPad()];\n",
     "\tshared_ptr<Player> player = mcle_container_player();\n", 1),
    ("\tpMinecraft->localgameModes[getPad()]->handleInventoryMouseClick(m_menu->containerId, slotId, buttonNum, quickKey, pMinecraft->localplayers[getPad()] );\n",
     "\tmcle_container_click(m_menu, slotId, buttonNum, quickKey);\n", 1),
    ("if( pMinecraft->localgameModes[getPad()] != nullptr",
     "if( mcle_container_game_mode() != nullptr", 2),
    ("if( Minecraft::GetInstance()->localgameModes[iPad] != nullptr )",
     "if( mcle_container_game_mode() != nullptr )", 1),
]

src = SRC.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
if src.count(START) != 1:
    print(f"constructor not found once in {SRC}")
    sys.exit(1)
block = src[src.index(START):]
for old, new, count in REPLACEMENTS:
    n = block.count(old)
    if n != count:
        print(f"expected {count} of {old.strip()!r}, found {n}")
        sys.exit(1)
    block = block.replace(old, new)
OUT.write_text(block, encoding="utf-8")
print(f"wrote {OUT} ({block.count(chr(10))} lines)")
