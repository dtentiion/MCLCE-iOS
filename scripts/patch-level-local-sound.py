#!/usr/bin/env python3
"""Give Level::playLocalSound a body on iOS.

Upstream's Level::playLocalSound is empty and MultiPlayerLevel overrides it,
since only the client's level makes sound. On iOS the server level is the
only level, so LevelRenderer::levelEvent (doors, clicks, fizz, anvils...)
and Tile::animateTick land in the empty one. This hands them to
Game/MCLESound.cpp, which does what MultiPlayerLevel::playLocalSound does.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.World" / "Level.cpp"
MARKER = "mcle_level_play_local_sound"

src = TARGET.read_text(encoding="utf-8", errors="replace")
if MARKER in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

nl = "\r\n" if "\r\n" in src else "\n"
OLD = ("void Level::playLocalSound(double x, double y, double z, int iSound, float volume, float pitch, "
       "bool distanceDelay, float fClipSoundDist)" + nl + "{" + nl + "}")
if src.count(OLD) != 1:
    print(f"Level::playLocalSound not found: {TARGET}")
    sys.exit(1)
NEW = ("#ifdef __APPLE_IOS__" + nl +
       "void mcle_level_play_local_sound(double x, double y, double z, int iSound, float volume, float pitch, "
       "bool distanceDelay, float fClipSoundDist);" + nl +
       "#endif" + nl + nl +
       "void Level::playLocalSound(double x, double y, double z, int iSound, float volume, float pitch, "
       "bool distanceDelay, float fClipSoundDist)" + nl + "{" + nl +
       "#ifdef __APPLE_IOS__" + nl +
       "\tmcle_level_play_local_sound(x, y, z, iSound, volume, pitch, distanceDelay, fClipSoundDist);" + nl +
       "#endif" + nl + "}")
TARGET.write_text(src.replace(OLD, NEW), encoding="utf-8", errors="replace", newline="")
print(f"patched: {TARGET}")
