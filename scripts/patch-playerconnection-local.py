#!/usr/bin/env python3
"""PlayerConnection.cpp builds, for the players who join.

The player whose game it is still has no client and no connection: its
ServerPlayer's connection is null, or a zeroed stand-in while it respawns or
crafts, and the server code calls send, teleport and a few others on it all
the same. Those used to be stubs of the whole class. Now the real ones start
by asking whether the connection is the local player's and, if so, do what
the stubs did.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CPP = REPO / "upstream" / "Minecraft.Client" / "PlayerConnection.cpp"
MARK = "MCLE_iOS_LOCAL_CONNECTION"

DECLS = (
    "#ifdef __APPLE_IOS__ // MCLE_iOS_LOCAL_CONNECTION\n"
    "// The player whose game this is has no connection: its ServerPlayer's is\n"
    "// null, or a zeroed stand-in while it respawns or crafts. What's sent to it\n"
    "// goes to the game loop, which is its client.\n"
    "bool mcle_connection_is_local(const PlayerConnection *connection);		// Game/MCLENetwork.cpp\n"
    "void mcle_local_player_packet(std::shared_ptr<Packet> packet);			// WorldProbe/link_stubs.cpp\n"
    "void mcle_local_player_abs_move_to(double x, double y, double z, float yRot, float xRot, bool sendPacket);\n"
    "#endif\n"
    "\n"
)


def guard(body):
    return "#ifdef __APPLE_IOS__\n\tif (mcle_connection_is_local(this))\n\t{\n" + body + "\t}\n#endif\n"


EDITS = [
    ("void PlayerConnection::teleport(double x, double y, double z, float yRot, float xRot, bool sendPacket /*= true*/)\n{\n",
     guard("\t\tmcle_local_player_abs_move_to(x, y, z, yRot, xRot, sendPacket);\n\t\treturn;\n")),
    ("void PlayerConnection::send(shared_ptr<Packet> packet)\n{\n",
     guard("\t\tmcle_local_player_packet(packet);\n\t\treturn;\n")),
    ("void PlayerConnection::queueSend(shared_ptr<Packet> packet)\n{\n",
     guard("\t\treturn;\n")),
    ("int PlayerConnection::countDelayedPackets()\n{\n",
     guard("\t\treturn 0;\n")),
    ("INetworkPlayer *PlayerConnection::getNetworkPlayer()\n{\n",
     guard("\t\treturn nullptr;\n")),
    ("bool PlayerConnection::isLocal()\n{\n",
     guard("\t\treturn true;\n")),
]

if not CPP.exists():
    sys.exit(f"missing: {CPP}")
src = CPP.read_text(encoding="utf-8", errors="replace")
if MARK in src:
    print(f"already patched: {CPP}")
    sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"
for i, (anchor, added) in enumerate(EDITS):
    anchor = anchor.replace("\n", nl)
    if src.count(anchor) != 1:
        sys.exit(f"anchor found {src.count(anchor)} times: {anchor[:60]!r}")
    new = anchor + added.replace("\n", nl)
    if i == 0:
        new = DECLS.replace("\n", nl) + new
    src = src.replace(anchor, new)
CPP.write_text(src, encoding="utf-8", errors="replace", newline="")
print(f"patched: {CPP}")
