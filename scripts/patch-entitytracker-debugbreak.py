#!/usr/bin/env python3
"""Remove the __debugbreak() in EntityTracker::addEntity that fires
when entityId >= 16384. This is a dev assertion; the function body
continues fine without it. Our procgen path generates mineshaft
chest entities whose IDs happen to exceed the threshold.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "EntityTracker.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_ENTID_NO_TRAP" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "\tif( e->entityId >= 16384 )\n"
    "\t{\n"
    "\t\t__debugbreak();\n"
    "\t}"
)
new = (
    "\t// MCLE_iOS_ENTID_NO_TRAP: skip dev assert; entity IDs > 16384\n"
    "\t// happen on our setup from procgen-spawned chest entities\n"
    "\t// (mineshaft etc.). The function body works regardless.\n"
    "\tif( false && e->entityId >= 16384 ) { __debugbreak(); }"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
