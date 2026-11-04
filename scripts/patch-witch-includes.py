#!/usr/bin/env python3
"""Includes the Witch renderer and model get from the PC precompiled header.

WitchRenderer.cpp casts to Mob and WitchModel.cpp reads Entity fields, but
neither includes the class. Idempotent.
"""
from pathlib import Path

CLIENT = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client"

for name, anchor, include in (
    ("WitchRenderer.cpp", '#include "WitchRenderer.h"', '#include "../Minecraft.World/Mob.h"'),
    ("WitchModel.cpp", '#include "WitchModel.h"', '#include "../Minecraft.World/Entity.h"'),
):
    path = CLIENT / name
    src = path.read_text(encoding="utf-8", errors="replace")
    if include in src:
        print(f"already patched: {path}")
        continue
    nl = "\r\n" if "\r\n" in src else "\n"
    if anchor not in src:
        raise SystemExit(f"anchor not found in {path}")
    path.write_text(src.replace(anchor, anchor + nl + include, 1), encoding="utf-8", errors="replace", newline="")
    print(f"added {include} to {path}")
