#!/usr/bin/env python3
"""Includes some renderers and models get from the PC precompiled header.

WitchRenderer.cpp casts to Mob, WitchModel.cpp reads Entity fields, and the
dragon / crystal files include EnderDragon.h, whose base class Mob isn't
included by it. Each gets the header it's missing ahead of its own.
Idempotent.
"""
from pathlib import Path

CLIENT = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client"
ENTITY = '#include "../Minecraft.World/net.minecraft.world.entity.h"'
DRAGON_SET = '#include "../Minecraft.World/net.minecraft.world.entity.boss.enderdragon.h"'

for name, anchor, include in (
    ("WitchRenderer.cpp", '#include "WitchRenderer.h"', '#include "../Minecraft.World/Mob.h"'),
    ("WitchModel.cpp", '#include "WitchModel.h"', '#include "../Minecraft.World/Entity.h"'),
    ("EnderDragonRenderer.cpp", DRAGON_SET, ENTITY),
    ("EnderCrystalRenderer.cpp", DRAGON_SET, ENTITY),
    ("DragonModel.cpp", '#include "../Minecraft.World/EnderDragon.h"', ENTITY),
):
    path = CLIENT / name
    src = path.read_text(encoding="utf-8", errors="replace")
    if include in src:
        print(f"already patched: {path}")
        continue
    nl = "\r\n" if "\r\n" in src else "\n"
    if anchor not in src:
        raise SystemExit(f"anchor not found in {path}")
    # the missing header goes in front of the one that needs it
    if include == ENTITY:
        patched = src.replace(anchor, include + nl + anchor, 1)
    else:
        patched = src.replace(anchor, anchor + nl + include, 1)
    path.write_text(patched, encoding="utf-8", errors="replace", newline="")
    print(f"added {include} to {path}")
