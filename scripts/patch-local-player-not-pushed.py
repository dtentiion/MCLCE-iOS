#!/usr/bin/env python3
"""Mobs and minecarts bumping into the local player don't push it.

There's no LocalPlayer on iOS, the local player is the ServerPlayer. Only
the server pushes entities apart (LivingEntity::aiStep's pushEntities and
Minecart::push are server only), and what it pushes its own player by is
lost: PlayerConnection::handleMovePlayer puts the player back where its
client moved it, and only a hurt sends the client its motion. So on TU19
nothing bumping into the player moves it, while it still pushes them.

Entity::push and Minecart::push leave out their push on the ServerPlayer,
which here is always the local player. Knockback from a hit is applied
apart from these and still moves it.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORLD = REPO / "upstream" / "Minecraft.World"
MARKER = "never reach the client"


def patch(path, old, new):
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    if MARKER in src:
        print(f"already patched: {path}")
        return
    nl = "\r\n" if "\r\n" in src else "\n"
    old = old.replace("\n", nl)
    new = new.replace("\n", nl)
    if src.count(old) != 1:
        sys.exit(f"anchor not found once in {path}")
    path.write_text(src.replace(old, new), encoding="utf-8", errors="replace", newline="")
    print(f"patched: {path}")


patch(WORLD / "Entity.cpp",
      "\t\tpush(-xa, 0, -za);\n"
      "\t\te->push(xa, 0, za);\n",
      "#ifdef __APPLE_IOS__\n"
      "\t\t// The server's pushes on its own player never reach the client\n"
      "\t\tif (GetType() != eTYPE_SERVERPLAYER) push(-xa, 0, -za);\n"
      "\t\tif (e->GetType() != eTYPE_SERVERPLAYER) e->push(xa, 0, za);\n"
      "#else\n"
      "\t\tpush(-xa, 0, -za);\n"
      "\t\te->push(xa, 0, za);\n"
      "#endif\n")

patch(WORLD / "Minecart.cpp",
      "\t\t\tpush(-xa, 0, -za);\n"
      "\t\t\te->push(xa / 4, 0, za / 4);\n",
      "\t\t\tpush(-xa, 0, -za);\n"
      "#ifdef __APPLE_IOS__\n"
      "\t\t\t// The server's pushes on its own player never reach the client\n"
      "\t\t\tif (e->GetType() != eTYPE_SERVERPLAYER)\n"
      "#endif\n"
      "\t\t\te->push(xa / 4, 0, za / 4);\n")
