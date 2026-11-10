#!/usr/bin/env python3
"""The sound listener is at the player's eyes.

SoundEngine::tick puts the listener at the player's y. That's the client's
player, whose y is heightOffset (1.62) above its feet. The player it's given
here is the server's, with its y at its feet, so the listener was on the
ground: the player's own sounds, which play at its feet, were at no distance
at all and came out louder than they should. On iOS the listener goes at
the y the client's player would have.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CPP = REPO / "upstream" / "Minecraft.Client" / "Common" / "Audio" / "SoundEngine.cpp"
MARK = "MCLE_iOS_LISTENER_EYE"

OLD = "\t\t\t\ty=players[i]->yo + (players[i]->y - players[i]->yo) * a;\n"
NEW = (OLD +
       "#ifdef __APPLE_IOS__\n"
       "\t\t\t\t// MCLE_iOS_LISTENER_EYE: the client player's y, heightOffset above the feet\n"
       "\t\t\t\ty += 1.62f - players[i]->heightOffset;\n"
       "#endif\n")

if not CPP.exists():
    sys.exit(f"missing: {CPP}")
src = CPP.read_text(encoding="utf-8", errors="replace")
if MARK in src:
    print(f"already patched: {CPP}")
    sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"
old = OLD.replace("\n", nl)
if src.count(old) != 1:
    sys.exit(f"anchor not found once in {CPP}")
src = src.replace(old, NEW.replace("\n", nl))
CPP.write_text(src, encoding="utf-8", errors="replace", newline="")
print(f"patched: {CPP}")
