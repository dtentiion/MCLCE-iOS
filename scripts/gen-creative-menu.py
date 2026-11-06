#!/usr/bin/env python3
"""Pull IUIScene_CreativeMenu's body out of upstream.

IUIScene_CreativeMenu.cpp is the creative menu's item table (staticCtor's
category groups and the tabs built from them), the paging, and picking
items onto the pointer and into the hotbar. It includes the Iggy UI.h, so
its functions are copied into upstream/Minecraft.Client/iOS_CreativeMenu.inc
and compiled by Game/MCLECreativeMenu.cpp against the container menus'
`ui` stand-in.

What changes:
- The local player here is the server player, so the inventory and the
  clicks are mcle_container_player()'s.
- handleCreativeModeItemAdd / handleCreativeModeItemDrop would send a
  SetCreativeModeSlotPacket to a server that is this same player, so they
  go to mcle_creative_item_add / mcle_creative_item_drop, the server's
  PlayerConnection::handleSetCreativeModeSlot.

Idempotent, rewrites the .inc every run.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "UI" / "IUIScene_CreativeMenu.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_CreativeMenu.inc"

START = "IUIScene_CreativeMenu::TabSpec **IUIScene_CreativeMenu::specs = nullptr;\n"

REPLACEMENTS = [
    (r"pMinecraft->localgameModes\[(?:iPad|getPad\(\))\]->handleCreativeModeItemAdd\(", "mcle_creative_item_add(", 2),
    (r"pMinecraft->localgameModes\[(?:iPad|getPad\(\))\]->handleCreativeModeItemDrop\(", "mcle_creative_item_drop(", 2),
    (r"pMinecraft->localplayers\[(?:iPad|getPad\(\))\]", "mcle_container_player()", 4),
]


def fail(msg):
    print(msg)
    sys.exit(1)


src = SRC.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
if src.count(START) != 1:
    fail(f"tab table not found once in {SRC}")
block = src[src.index(START):]

for pattern, new, count in REPLACEMENTS:
    block, n = re.subn(pattern, new, block)
    if n != count:
        fail(f"expected {count} of {pattern!r}, found {n}")

code = "\n".join(l for l in block.split("\n") if not l.lstrip().startswith("//"))
if "localgameModes" in code or "localplayers" in code:
    fail("client player or game mode code left in the creative menu body")

OUT.write_text(block, encoding="utf-8")
print(f"wrote {OUT} ({block.count(chr(10))} lines)")
