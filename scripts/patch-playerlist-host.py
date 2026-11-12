#!/usr/bin/env python3
"""The host isn't in the player list, and the list's code counts on it.

In upstream the host's player logs in like any other and is one of
PlayerList::players. Here it is made by the game loop and kept out of the
list. For the players who do join through the list, the places that look
through it for "everyone" get the host as well:

- placeNewPlayer hands out the lowest player index nobody has, so the
  host's is marked taken
- add sends the joiner everyone's PlayerInfoPacket and, if someone is in
  bed, who: the host's packet goes too, and the host can be the one asleep
- the host taking a portal or respawning looks for another player on its
  own system in the dimension it leaves, through its network player, which
  it hasn't one of
- a joiner's name and id are checked against the list for one that's taken

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CLIENT = REPO / "upstream" / "Minecraft.Client"
MARK = "MCLE_iOS_HOST_PLAYER"


def patch(path, pairs):
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    if MARK in src:
        print(f"already patched: {path}")
        return
    nl = "\r\n" if "\r\n" in src else "\n"
    for old, new, count in pairs:
        old = old.replace("\n", nl)
        new = new.replace("\n", nl)
        if src.count(old) != count:
            sys.exit(f"anchor found {src.count(old)} times in {path.name}, wanted {count}: {old[:70]!r}")
        src = src.replace(old, new)
    path.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {path}")


patch(CLIENT / "PlayerList.cpp", [
    ('#include "PlayerList.h"\n',
     '#include "PlayerList.h"\n'
     "#ifdef __APPLE_IOS__ // MCLE_iOS_HOST_PLAYER\n"
     "shared_ptr<ServerPlayer> mcle_local_player(); // Game/MCLEGameLoop.cpp\n"
     "#endif\n", 1),
    ("\t\t\tusedIndexes[p->getPlayerIndex()] = true;\n\t\t}\n",
     "\t\t\tusedIndexes[p->getPlayerIndex()] = true;\n\t\t}\n"
     "#ifdef __APPLE_IOS__\n"
     "\t\t// and the host's\n"
     "\t\tif (shared_ptr<ServerPlayer> host = mcle_local_player()) usedIndexes[host->getPlayerIndex()] = true;\n"
     "#endif\n", 1),
    ("\t\t\tplayer->connection->send(std::make_shared<PlayerInfoPacket>(op));\n\t\t}\n\t}\n",
     "\t\t\tplayer->connection->send(std::make_shared<PlayerInfoPacket>(op));\n\t\t}\n\t}\n"
     "#ifdef __APPLE_IOS__\n"
     "\t// and the host's\n"
     "\tif (shared_ptr<ServerPlayer> host = mcle_local_player()) player->connection->send(std::make_shared<PlayerInfoPacket>(host));\n"
     "#endif\n", 1),
    ("\t\tplayer->connection->send(std::make_shared<ChatPacket>(firstSleepingPlayer->name, ChatPacket::e_ChatBedPlayerSleep));\n",
     "#ifdef __APPLE_IOS__\n"
     "\t\t// The host can be the one asleep\n"
     "\t\tif (firstSleepingPlayer == nullptr)\n"
     "\t\t{\n"
     "\t\t\tshared_ptr<ServerPlayer> host = mcle_local_player();\n"
     "\t\t\tif (host != nullptr && host->isSleeping()) firstSleepingPlayer = host;\n"
     "\t\t}\n"
     "\t\tif (firstSleepingPlayer != nullptr)\n"
     "#endif\n"
     "\t\tplayer->connection->send(std::make_shared<ChatPacket>(firstSleepingPlayer->name, ChatPacket::e_ChatBedPlayerSleep));\n", 1),
    # placeNewPlayer, respawn and changeDimension. In the last two it can be
    # the host, who has no network player.
    ("if( otherPlayer != nullptr && thisPlayer->IsSameSystem(otherPlayer) )",
     "if( otherPlayer != nullptr && thisPlayer != nullptr && thisPlayer->IsSameSystem(otherPlayer) )", 3),
])

patch(CLIENT / "PendingConnection.cpp", [
    ('#include "PendingConnection.h"\n',
     '#include "PendingConnection.h"\n'
     "#ifdef __APPLE_IOS__ // MCLE_iOS_HOST_PLAYER\n"
     "shared_ptr<ServerPlayer> mcle_local_player();\n"
     "#endif\n", 1),
    ("\tbool duplicateXuid = false;\n",
     "\tbool duplicateXuid = false;\n"
     "#ifdef __APPLE_IOS__\n"
     "\t// The host isn't in the player list getPlayer looks through\n"
     "\tshared_ptr<ServerPlayer> mcleHost = mcle_local_player();\n"
     "\tif (mcleHost != nullptr && loginXuid != INVALID_XUID && mcleHost->getXuid() == loginXuid)\n"
     "\t{\n"
     "\t\tduplicateXuid = true;\n"
     "\t}\n"
     "\telse\n"
     "#endif\n", 1),
    ("\t\tvector<shared_ptr<ServerPlayer> >& pl = server->getPlayers()->players;\n",
     "\t\tvector<shared_ptr<ServerPlayer> >& pl = server->getPlayers()->players;\n"
     "#ifdef __APPLE_IOS__\n"
     "\t\tif (mcleHost != nullptr && mcleHost->name == name) nameTaken = true;\n"
     "#endif\n", 1),
])
