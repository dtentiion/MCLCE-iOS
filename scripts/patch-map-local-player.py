#!/usr/bin/env python3
"""MapItemSavedData::tickCarriedBy also counts the port's player.

Maps put a marker on them for each player in the PlayerList, and check it
for a player in The End before showing the End portal. The port's player is
the server's own, kept out of `players`, so a held map never got its
holder's marker. On iOS both loops also go over the local player.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.World" / "MapItemSavedData.cpp"

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
with open(SRC, encoding="utf-8", newline="") as f:
    src = f.read()
if "mcle_map_players" in src:
    print(f"already patched: {SRC}")
    sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"

LOOPS = (
    "\t\t\t\tfor( const auto& serverPlayer : players->players)",
    "\t\t\t\tfor(auto& decorationPlayer : players->players)",
)
for loop in LOOPS:
    anchor = loop + nl
    if src.count(anchor) != 1:
        sys.exit(f"loop anchor not found once in {SRC}")
    src = src.replace(anchor, nl.join([
        "#ifdef __APPLE_IOS__",
        loop.replace("players->players", "mcle_map_players(players)"),
        "#else",
        loop,
        "#endif",
        "",
    ]))

ANCHOR = "const int MapItemSavedData::END_PORTAL_DECORATION_KEY = -1;" + nl
if src.count(ANCHOR) != 1:
    sys.exit(f"END_PORTAL_DECORATION_KEY anchor not found once in {SRC}")
helper = nl.join([
    "#ifdef __APPLE_IOS__",
    "// Game/MCLEGameLoop.cpp: the port's player isn't in the PlayerList's players",
    "shared_ptr<ServerPlayer> mcle_local_player();",
    "",
    "static vector<shared_ptr<ServerPlayer> > mcle_map_players(PlayerList *players)",
    "{",
    "\tvector<shared_ptr<ServerPlayer> > mapPlayers = players->players;",
    "\tshared_ptr<ServerPlayer> localPlayer = mcle_local_player();",
    "\tif (localPlayer != nullptr && find(mapPlayers.begin(), mapPlayers.end(), localPlayer) == mapPlayers.end())",
    "\t{",
    "\t\tmapPlayers.push_back(localPlayer);",
    "\t}",
    "\treturn mapPlayers;",
    "}",
    "#endif",
    "",
    "",
])
src = src.replace(ANCHOR, helper + ANCHOR)

with open(SRC, "w", encoding="utf-8", newline="") as f:
    f.write(src)
print(f"patched: {SRC}")
