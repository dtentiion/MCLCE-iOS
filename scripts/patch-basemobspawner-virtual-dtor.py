#!/usr/bin/env python3
"""Make BaseMobSpawner's destructor virtual.

MobSpawnerTileEntity::~MobSpawnerTileEntity does `delete spawner` through a
BaseMobSpawner*. The dtor isn't virtual and the class is abstract, which
MSVC lets through but clang compiles into a trap (brk #1) in the deleting
destructor. So the game died the moment a chunk with a dungeon spawner was
unloaded. Virtual is what the code meant anyway.

Idempotent.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.World" / "BaseMobSpawner.h"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if re.search(r"virtual\s+~BaseMobSpawner\(\);", src):
    print(f"already patched: {TARGET}")
    sys.exit(0)

new, n = re.subn(r"(\t)~BaseMobSpawner\(\);", r"\1virtual ~BaseMobSpawner();", src, count=1)
if n != 1:
    sys.exit("BaseMobSpawner dtor anchor not found")

TARGET.write_text(new, encoding="utf-8", errors="replace", newline="")
print(f"patched: {TARGET}")
