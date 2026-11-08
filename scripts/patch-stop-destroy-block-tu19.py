#!/usr/bin/env python3
"""A finished break destroys the block straight away, like TU19.

TU19's ServerPlayerGameMode::stopDestroyBlock doesn't count the progress
again, the client already did before it sent the stop (MGH's note: it
failed to destroy when packets bunched up). The dedicated server added a
recount, the ticks since the start times the dig speed right now. When
that speed has dropped, like jumping or stepping off the ground just as
the block finishes, the recount comes up short and the block waits for
the delayed destroy. A client takes the block away from its own level
when it finishes, but the local player sees the server's level, so on
iOS the block stayed until the delayed destroy caught up and some breaks
came late. iOS gets TU19's body back.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "ServerPlayerGameMode.cpp"

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
src = SRC.read_bytes().decode("utf-8", errors="replace")
nl = "\r\n" if "\r\n" in src else "\n"
OLD = ("\t\t\t// Anti-cheat: re-check destroy progress on the server for STOP_DESTROY.\n"
       "\t\t\tint ticksSpentDestroying = gameTicks - destroyProgressStart;\n"
       "\t\t\tfloat destroyProgress = tile->getDestroyProgress(player, player->level, x, y, z) * (ticksSpentDestroying + 1);\n"
       "\t\t\tif (destroyProgress >= 1.0f)\n")
NEW = ("#ifdef __APPLE_IOS__\n"
       "\t\t\t// TU19: the client checked the progress before it sent the stop\n"
       "\t\t\tif (true)\n"
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
