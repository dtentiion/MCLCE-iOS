#!/usr/bin/env python3
"""What a server leaves to a player's own client isn't shown to that player.

A server skips the player an effect comes from when it sends it out: that
player's client has already done it for itself. Two places only ever run on
the server though, so the player they're for gets nothing in the original:

Inventory::hurtArmor. Armour wears and breaks where the damage is dealt.
LivingEntity::breakItem's sound goes out through Player::playSound, which
skips the player, and its particles go to ServerLevelListener::addParticle,
which does nothing. A player doesn't hear or see its own armour break.

ServerPlayerGameMode::startDestroyBlock, outside creative. The client only
calls Level::extinguishFire from creativeDestroyBlock, and the fizz the
server sends for a fire hit in survival skips the one who hit it.

There's one level and one player here, so both were heard, and the armour's
particles drawn. On iOS the two are marked while they run and the port's
listeners let those go (Game/MCLEGameLoop.cpp, Game/MCLESound.cpp).

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UPSTREAM = REPO / "upstream"
MARK = "MCLE_iOS_SERVER_ONLY_OWN"

SCOPE = ("#ifdef __APPLE_IOS__\n"
         "// MCLE_iOS_SERVER_ONLY_OWN: Game/MCLEGameLoop.cpp\n"
         "extern \"C\" void mcle_server_only_own(int on);\n"
         "namespace {\n"
         "struct MCLEServerOnlyOwn\n"
         "{\n"
         "\tMCLEServerOnlyOwn() { mcle_server_only_own(1); }\n"
         "\t~MCLEServerOnlyOwn() { mcle_server_only_own(0); }\n"
         "};\n"
         "}\n"
         "#endif\n"
         "\n")

FILES = [
    (UPSTREAM / "Minecraft.World" / "Inventory.cpp", [
        ("void Inventory::hurtArmor(float dmg)\n{\n",
         SCOPE + "void Inventory::hurtArmor(float dmg)\n{\n"
         "#ifdef __APPLE_IOS__\n"
         "\tMCLEServerOnlyOwn serverOnlyOwn;\n"
         "#endif\n"),
    ]),
    (UPSTREAM / "Minecraft.Client" / "ServerPlayerGameMode.cpp", [
        ("void ServerPlayerGameMode::startDestroyBlock(int x, int y, int z, int face)\n",
         SCOPE + "void ServerPlayerGameMode::startDestroyBlock(int x, int y, int z, int face)\n"),
        ("\tlevel->extinguishFire(player, x, y, z, face);\n"
         "\tdestroyProgressStart = gameTicks;\n",
         "#ifdef __APPLE_IOS__\n"
         "\t{\n"
         "\t\tMCLEServerOnlyOwn serverOnlyOwn;\n"
         "\t\tlevel->extinguishFire(player, x, y, z, face);\n"
         "\t}\n"
         "#else\n"
         "\tlevel->extinguishFire(player, x, y, z, face);\n"
         "#endif\n"
         "\tdestroyProgressStart = gameTicks;\n"),
    ]),
]

for path, pairs in FILES:
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    if MARK in src:
        print(f"already patched: {path}")
        continue
    nl = "\r\n" if "\r\n" in src else "\n"
    for old, new in pairs:
        o = old.replace("\n", nl)
        if src.count(o) != 1:
            sys.exit(f"anchor not found once in {path}: {old.splitlines()[0]}")
        src = src.replace(o, new.replace("\n", nl))
    path.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {path}")
