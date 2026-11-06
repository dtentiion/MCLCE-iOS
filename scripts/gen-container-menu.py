#!/usr/bin/env python3
"""Pull IUIScene_AbstractContainerMenu's body out of upstream.

IUIScene_AbstractContainerMenu.cpp is the pointer, slot and tooltip logic
every container menu shares, but it includes the Iggy UI.h, so it can't be
built as it is. Its functions are copied into
upstream/Minecraft.Client/iOS_AbstractContainerMenu.inc and compiled by
Game/MCLEContainerMenu.cpp against small stand-ins for `ui` and the
Windows64 keyboard and mouse.

What changes:
- The local player here is the server player. The carried item comes off
  it, and a slot click goes to its container the way
  PlayerConnection::handleContainerClick does (handleInventoryMouseClick
  would send a packet to a server that is this same player).
- Initialize doesn't put the menu on the player. The client sets its own
  LocalPlayer's, but the server player already has the container the
  server opened, and the creative menu's item picker is the client's alone.
- The tutorial is Game/MCLETutorial.cpp's, there's no game mode in
  Minecraft::localgameModes to ask for it.

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
     "\t// the server player keeps the container the server opened\n", 1),
    ("\tshared_ptr<LocalPlayer> player = Minecraft::GetInstance()->localplayers[getPad()];\n",
     "\tshared_ptr<Player> player = mcle_container_player();\n", 1),
    ("\tpMinecraft->localgameModes[getPad()]->handleInventoryMouseClick(m_menu->containerId, slotId, buttonNum, quickKey, pMinecraft->localplayers[getPad()] );\n",
     "\tmcle_container_click(m_menu, slotId, buttonNum, quickKey);\n", 1),
]

# The tutorial the game mode has, and the game mode's being there
TUTORIAL = [
    ("pMinecraft->localgameModes[getPad()] != nullptr", "mcle_tutorial() != nullptr", 2),
    ("pMinecraft->localgameModes[getPad()]->getTutorial()", "mcle_tutorial()", 2),
    ("Minecraft::GetInstance()->localgameModes[iPad] != nullptr", "mcle_tutorial() != nullptr", 1),
    ("TutorialMode *gameMode = static_cast<TutorialMode *>(Minecraft::GetInstance()->localgameModes[iPad]);",
     "Tutorial *tutorial = mcle_tutorial();", 1),
    ("gameMode->getTutorial()->setMessage(nullptr, message);", "tutorial->setMessage(nullptr, message);", 1),
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

for old, new, count in TUTORIAL:
    n = block.count(old)
    if n != count:
        print(f"expected {count} of {old!r}, found {n}")
        sys.exit(1)
    block = block.replace(old, new)
code = "\n".join(l for l in block.split("\n") if not l.lstrip().startswith("//"))
if "localgameModes" in code or "TutorialMode" in code:
    print("game mode code left in the container menu body")
    sys.exit(1)

OUT.write_text(block, encoding="utf-8")
print(f"wrote {OUT} ({block.count(chr(10))} lines)")
