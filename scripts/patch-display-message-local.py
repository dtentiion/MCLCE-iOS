#!/usr/bin/env python3
"""ServerPlayer::displayClientMessage also reaches the port's player.

The spawn egg, breeding, boat and hanging entity limits, the peaceful
spawn warning and the other 4J chat lines a player sends itself go out by
looping over the PlayerList's players and sending to the one that is this
player. The port's player is the server's own, kept out of `players`, so
none of them ever showed. On iOS the local player's own messages loop over
the list with it in, as scripts/patch-map-local-player.py does for maps.
Only its own: PlayerList::respawn's new player tells the others it left
The End while the old one is still the local player.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "ServerPlayer.cpp"

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
with open(SRC, encoding="utf-8", newline="") as f:
    src = f.read()
if "mcle_message_players" in src:
    print(f"already patched: {SRC}")
    sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"

START = "void ServerPlayer::displayClientMessage(int messageId)" + nl + "{" + nl
END = "void ServerPlayer::completeUsingItem()"
if src.count(START) != 1 or src.count(END) != 1:
    sys.exit(f"displayClientMessage anchors not found once in {SRC}")
start = src.index(START) + len(START)
end = src.index(END)
body = src[start:end]

LOOP = nl.join([
    "\t\tfor (unsigned int i = 0; i < server->getPlayers()->players.size(); i++)",
    "\t\t{",
    "\t\t\tshared_ptr<ServerPlayer> player = server->getPlayers()->players[i];",
    "",
])
loops = body.count(LOOP)
if loops == 0 or loops != body.count("server->getPlayers()->players.size()"):
    sys.exit(f"displayClientMessage loops not as expected in {SRC}")
body = body.replace(LOOP, nl.join([
    "#ifdef __APPLE_IOS__",
    "\t\tfor (unsigned int i = 0; i < messagePlayers.size(); i++)",
    "\t\t{",
    "\t\t\tshared_ptr<ServerPlayer> player = messagePlayers[i];",
    "#else",
    LOOP + "#endif",
    "",
]))
body = nl.join([
    "#ifdef __APPLE_IOS__",
    "\tvector<shared_ptr<ServerPlayer> > messagePlayers = mcle_message_players(server->getPlayers(), this);",
    "#endif",
    "",
]) + body

helper = nl.join([
    "#ifdef __APPLE_IOS__",
    "// Game/MCLEGameLoop.cpp: the port's player isn't in the PlayerList's players",
    "shared_ptr<ServerPlayer> mcle_local_player();",
    "",
    "static vector<shared_ptr<ServerPlayer> > mcle_message_players(PlayerList *players, ServerPlayer *self)",
    "{",
    "\tvector<shared_ptr<ServerPlayer> > messagePlayers = players->players;",
    "\tshared_ptr<ServerPlayer> localPlayer = mcle_local_player();",
    "\tif (localPlayer.get() == self && find(messagePlayers.begin(), messagePlayers.end(), localPlayer) == messagePlayers.end())",
    "\t{",
    "\t\tmessagePlayers.push_back(localPlayer);",
    "\t}",
    "\treturn messagePlayers;",
    "}",
    "#endif",
    "",
    "",
])
fn = src.index(START)
src = src[:fn] + helper + src[fn:start] + body + src[end:]

with open(SRC, "w", encoding="utf-8", newline="") as f:
    f.write(src)
print(f"patched: {SRC} ({loops} loops)")
