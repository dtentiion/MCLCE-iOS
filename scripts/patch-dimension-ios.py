#!/usr/bin/env python3
"""Portals for the local player, who is the server's own ServerPlayer.

ServerPlayer::changeDimension starts with connection->hasClientTickedOnce(),
and the local player has no connection, so walking into a portal crashed.
Its client is the game itself and has always ticked. The server changes
the level in the middle of the player's tick, where the port holds the
frame, so on iOS the change goes to Game/MCLEGameLoop.cpp, which makes it
once the tick lets the frame go (finding or building the arrival portal
can take a while) with a stand-in connection.

The End exit sends WIN_GAME with the network player's pad. There's no
network player here, the local player is the primary pad.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UP = REPO / "upstream"
MARK = "mcle_defer_change_dimension"


def patch(path, edits):
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_bytes().decode("utf-8", errors="replace")
    nl = "\r\n" if "\r\n" in src else "\n"
    done = 0
    for old, new in edits:
        old = old.replace("\n", nl)
        new = new.replace("\n", nl)
        if new in src:
            continue
        if src.count(old) != 1:
            sys.exit(f"anchor not found once in {path}: {old[:60]!r}")
        src = src.replace(old, new)
        done += 1
    if done:
        path.write_text(src, encoding="utf-8", errors="replace", newline="")
        print(f"patched: {path} ({done})")
    else:
        print(f"already patched: {path}")


patch(UP / "Minecraft.Client" / "ServerPlayer.cpp", [
    ("void ServerPlayer::changeDimension(int i)\n"
     "{\n"
     "\tif(!connection->hasClientTickedOnce()) return;\n",
     "#ifdef __APPLE_IOS__\n"
     "bool " + MARK + "(ServerPlayer *player, int i);\n"
     "#endif\n"
     "\n"
     "void ServerPlayer::changeDimension(int i)\n"
     "{\n"
     "#ifdef __APPLE_IOS__\n"
     "\t// The local player's client is the game, it has always ticked\n"
     "\tif (" + MARK + "(this, i)) return;\n"
     "#else\n"
     "\tif(!connection->hasClientTickedOnce()) return;\n"
     "#endif\n"),
    ("\t\t\tconnection->send(std::make_shared<GameEventPacket>(GameEventPacket::WIN_GAME, thisPlayer->GetUserIndex()));\n"
     "\t\t\tapp.DebugPrintf(\"Sending packet to %d\\n\", thisPlayer->GetUserIndex());\n",
     "#ifdef __APPLE_IOS__\n"
     "\t\t\t// No network player, the local player is the primary pad\n"
     "\t\t\tconnection->send(std::make_shared<GameEventPacket>(GameEventPacket::WIN_GAME, thisPlayer ? thisPlayer->GetUserIndex() : 0));\n"
     "#else\n"
     "\t\t\tconnection->send(std::make_shared<GameEventPacket>(GameEventPacket::WIN_GAME, thisPlayer->GetUserIndex()));\n"
     "\t\t\tapp.DebugPrintf(\"Sending packet to %d\\n\", thisPlayer->GetUserIndex());\n"
     "#endif\n"),
])
