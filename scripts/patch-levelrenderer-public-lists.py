#!/usr/bin/env python3
"""Expose LevelRenderer::starList (and friends) as public.

Our MCLEGameLoop needs to read starList right after LevelRenderer
construction so we can register the world-decoration list IDs
(starList/skyList/darkList/haloRingList/cloudList) with the
auto-replay skip set. The header declares them in the private:
section.

We insert a 'public:' just before the line and a matching 'private:'
after it so only those two lines flip access and the rest of the
private section stays private.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.h"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_PUBLIC_LISTS" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = "\tint ticks;\n\tint starList, skyList, darkList, haloRingList;\n\tint cloudList;\t// 4J added"
new = (
    "\tint ticks;\n"
    "public: // MCLE_iOS_PUBLIC_LISTS\n"
    "\tint starList, skyList, darkList, haloRingList;\n"
    "\tint cloudList;\t// 4J added\n"
    "private:"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
