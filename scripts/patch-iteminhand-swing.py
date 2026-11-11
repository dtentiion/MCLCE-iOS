#!/usr/bin/env python3
"""The held item swings the way it does on Windows64.

ItemInHandRenderer::render raises the swing to a power: 1 on Windows64, PS3
and PS4, 4 on the rest, which 4J added to slow the start of the swing down
on the 360. iOS isn't one of the names in that #if, so it got the 4.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CPP = REPO / "upstream" / "Minecraft.Client" / "ItemInHandRenderer.cpp"
MARK = "MCLE_iOS_SWING_POW"

OLD = ("#if defined __ORBIS__ || defined __PS3__ || defined _WINDOWS64\n"
       "\t\tstatic const float swingPowFactor = 1.0f;\n")
NEW = ("#if defined __ORBIS__ || defined __PS3__ || defined _WINDOWS64 || defined __APPLE_IOS__ // MCLE_iOS_SWING_POW\n"
       "\t\tstatic const float swingPowFactor = 1.0f;\n")

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
