#!/usr/bin/env python3
"""Pull IUIScene_BeaconMenu's body out of upstream.

IUIScene_BeaconMenu.cpp is the beacon menu's power buttons, hover text and
confirm. The confirm sends SET_BEACON_PACKET through the local player's
connection, which the server player here doesn't have the client's kind
of, so the file is left out of the world library and its functions are
copied into upstream/Minecraft.Client/iOS_BeaconMenu.inc for
Game/MCLEContainerMenus.cpp.

What changes:
- The SET_BEACON_PACKET goes to mcle_beacon_set_powers, the server's
  PlayerConnection::handleCustomPayload for it, as the server is this
  same player.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "UI" / "IUIScene_BeaconMenu.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_BeaconMenu.inc"

START = "IUIScene_BeaconMenu::IUIScene_BeaconMenu()\n"

REPLACEMENTS = [
    ("Minecraft::GetInstance()->localplayers[getPad()]->connection->send(std::make_shared<CustomPayloadPacket>"
     "(CustomPayloadPacket::SET_BEACON_PACKET, baos.toByteArray()));",
     "mcle_beacon_set_powers(m_beacon->getPrimaryPower(), m_beacon->getSecondaryPower());", 1),
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
if "localplayers" in code or "connection->" in code or "Minecraft::GetInstance" in code:
    fail("client player code left in the beacon menu body")

OUT.write_text(block, encoding="utf-8")
print(f"wrote {OUT} ({block.count(chr(10))} lines)")
