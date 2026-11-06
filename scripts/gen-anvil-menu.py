#!/usr/bin/env python3
"""Pull IUIScene_AnvilMenu's body out of upstream.

IUIScene_AnvilMenu.cpp is the anvil menu's cost, cross and item name
logic. updateItemName sends the name to the server through the local
player's connection, which the server player here doesn't have the
client's kind of, so the file is left out of the world library and its
functions are copied into upstream/Minecraft.Client/iOS_AnvilMenu.inc for
Game/MCLEContainerMenus.cpp.

What changes:
- The SET_ITEM_NAME_PACKET goes to mcle_anvil_set_item_name, the server's
  PlayerConnection::handleCustomPayload for it, as the server is this
  same player.
- handleTick reads the local player's abilities off the server player.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "UI" / "IUIScene_AnvilMenu.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_AnvilMenu.inc"

START = "IUIScene_AnvilMenu::IUIScene_AnvilMenu()\n"

REPLACEMENTS = [
    ("Minecraft::GetInstance()->localplayers[getPad()]->connection->send(std::make_shared<CustomPayloadPacket>"
     "(CustomPayloadPacket::SET_ITEM_NAME_PACKET, baos.toByteArray()));",
     "mcle_anvil_set_item_name(m_itemName);", 1),
    ("pMinecraft->localplayers[getPad()]->abilities.instabuild",
     "mcle_container_player()->abilities.instabuild", 1),
]


def fail(msg):
    print(msg)
    sys.exit(1)


src = SRC.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
if src.count(START) != 1:
    fail(f"constructor not found once in {SRC}")
block = src[src.index(START):]

for old, new, count in REPLACEMENTS:
    n = block.count(old)
    if n != count:
        fail(f"expected {count} of {old!r}, found {n}")
    block = block.replace(old, new)

code = "\n".join(l for l in block.split("\n") if not l.lstrip().startswith("//"))
if "localplayers" in code or "connection->" in code:
    fail("client player code left in the anvil menu body")

OUT.write_text(block, encoding="utf-8")
print(f"wrote {OUT} ({block.count(chr(10))} lines)")
