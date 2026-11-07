#!/usr/bin/env python3
"""An ender pearl landing teleports the local player.

ThrownEnderpearl::onHit only teleports a player whose connection isn't
done, and reads that from the player's connection. The local player is
the server's own ServerPlayer and has no connection, so the pearl
landing crashed. Its client is the game itself, which is never done
while the level ticks, so on iOS no connection counts as not done and
the teleport, the dismount and the fall damage go ahead like upstream.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.World" / "ThrownEnderpearl.cpp"

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
src = SRC.read_bytes().decode("utf-8", errors="replace")
nl = "\r\n" if "\r\n" in src else "\n"
OLD = "\t\t\t\tif(!serverPlayer->connection->done && serverPlayer->level == this->level)\n"
NEW = ("#ifdef __APPLE_IOS__\n"
       "\t\t\t\t// The local player has no connection, its client is never done\n"
       "\t\t\t\tif((serverPlayer->connection == nullptr || !serverPlayer->connection->done) && serverPlayer->level == this->level)\n"
       "#else\n" + OLD + "#endif\n")
OLD = OLD.replace("\n", nl)
NEW = NEW.replace("\n", nl)
if NEW in src:
    print(f"already patched: {SRC}")
    sys.exit(0)
if src.count(OLD) != 1:
    sys.exit(f"anchor not found once in {SRC}")
SRC.write_text(src.replace(OLD, NEW), encoding="utf-8", errors="replace", newline="")
print(f"patched: {SRC}")
