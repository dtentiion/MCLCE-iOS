#!/usr/bin/env python3
"""Particles only the server's level is given stay unseen.

Mob::spawnAnim (a spawner's mob, a silverfish going into or coming out of
stone) and BreedGoal::breed (the hearts round a newborn) add their particles
from code that only ever runs on the server. A server's level hands
particles to ServerLevelListener::addParticle, which does nothing, so no
player sees these in the original.

The renderer here listens to the server's level, and it drew them: a white
puff on each of those, and a burst of hearts when a baby is born. On iOS
the two are marked while they run, and the port's listener lets particles
go while the mark is set (Game/MCLEGameLoop.cpp). The loops still run, so
the random numbers they take are the same.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORLD = REPO / "upstream" / "Minecraft.World"
MARK = "MCLE_iOS_SERVER_ONLY_PARTICLES"

SCOPE = ("#ifdef __APPLE_IOS__\n"
         "// MCLE_iOS_SERVER_ONLY_PARTICLES: Game/MCLEGameLoop.cpp\n"
         "extern \"C\" void mcle_server_only_particles(int on);\n"
         "namespace {\n"
         "struct MCLEServerOnlyParticles\n"
         "{\n"
         "\tMCLEServerOnlyParticles() { mcle_server_only_particles(1); }\n"
         "\t~MCLEServerOnlyParticles() { mcle_server_only_particles(0); }\n"
         "};\n"
         "}\n"
         "#endif\n"
         "\n")

USE = ("#ifdef __APPLE_IOS__\n"
       "\tMCLEServerOnlyParticles serverOnlyParticles;\n"
       "#endif\n")

FILES = [
    ("Mob.cpp",
     "void Mob::spawnAnim()\n{\n",
     SCOPE + "void Mob::spawnAnim()\n{\n" + USE),
    ("BreedGoal.cpp",
     "void BreedGoal::breed()\n{\n",
     SCOPE + "void BreedGoal::breed()\n{\n" + USE),
]

for name, old, new in FILES:
    path = WORLD / name
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    if MARK in src:
        print(f"already patched: {path}")
        continue
    nl = "\r\n" if "\r\n" in src else "\n"
    o = old.replace("\n", nl)
    if src.count(o) != 1:
        sys.exit(f"anchor not found once in {path}")
    src = src.replace(o, new.replace("\n", nl))
    path.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {path}")
