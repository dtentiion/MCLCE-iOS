#!/usr/bin/env python3
"""PlayerList::broadcastAll also reaches the port's player.

The port's player is the server's own, kept out of `players` and with no
connection, so broadcasts (death and join messages) never got to it. On
iOS both broadcastAll overloads hand the packet to the same place
PlayerConnection::send does for it.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "PlayerList.cpp"

src = SRC.read_text(encoding="utf-8", errors="replace")
if "mcle_broadcast_to_local_player" in src:
    print(f"already patched: {SRC}")
    sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"

ALL = nl.join([
    "void PlayerList::broadcastAll(shared_ptr<Packet> packet)",
    "{",
    "\tfor (unsigned int i = 0; i < players.size(); i++)",
    "\t{",
    "\t\tshared_ptr<ServerPlayer> player = players[i];",
    "\t\tplayer->connection->send(packet);",
    "\t}",
])
DIM = nl.join([
    "void PlayerList::broadcastAll(shared_ptr<Packet> packet, int dimension)",
    "{",
    "\tfor (unsigned int i = 0; i < players.size(); i++)",
    "\t{",
    "\t\tshared_ptr<ServerPlayer> player = players[i];",
    "\t\tif (player->dimension == dimension) player->connection->send(packet);",
    "\t}",
])
for anchor, arg in ((ALL, "false, 0"), (DIM, "true, dimension")):
    if src.count(anchor) != 1:
        sys.exit(f"broadcastAll anchor not found once in {SRC}")
    src = src.replace(anchor, anchor + nl + nl.join([
        "#ifdef __APPLE_IOS__",
        f"\tmcle_broadcast_to_local_player(packet, {arg});",
        "#endif",
    ]))

decl = nl.join([
    "#ifdef __APPLE_IOS__",
    "// Game/MCLEGameLoop.cpp: the port's player isn't in players",
    "void mcle_broadcast_to_local_player(shared_ptr<Packet> packet, bool oneDimension, int dimension);",
    "#endif",
    "",
])
first = src.index("void PlayerList::")
line = src.rfind("\n", 0, first) + 1
src = src[:line] + decl + src[line:]
SRC.write_text(src, encoding="utf-8")
print(f"patched: {SRC}")
