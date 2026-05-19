#!/usr/bin/env python3
"""Force PlayerChunkMap::add to queue ALL chunks via getChunkAndAddPlayer
instead of synchronously creating the inner 14-ring (~196 chunks). That
synchronous load crashes our build (the 14-ring overlaps the chunk
regions r=5/r=7 preload tried before). Setting maxLegSizeToAddNow=0
forces every chunk through the async queue, which tick() drains one
per call.

Only the origin chunk stays synchronous (single getChunk(true) call
before the spiral) - one chunk is fine.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "PlayerChunkMap.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_PCM_ADD_ASYNC" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = "\tconst int maxLegSizeToAddNow = 14;"
new = (
    "\tconst int maxLegSizeToAddNow = 0; "
    "/* MCLE_iOS_PCM_ADD_ASYNC: queue all, drain via tick */"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
