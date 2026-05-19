#!/usr/bin/env python3
"""One-shot log inside ServerChunkCache::create so we can see if the
on-demand chunk streaming path actually reaches it.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "ServerChunkCache.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_SCC_CREATE_LOG" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = "LevelChunk *ServerChunkCache::create(int x, int z, bool asyncPostProcess)\t// 4J - added extra parameter\n{"
new = (
    "LevelChunk *ServerChunkCache::create(int x, int z, bool asyncPostProcess)\n{\n"
    "\t// MCLE_iOS_SCC_CREATE_LOG: log every entry so we can see if\n"
    "\t// on-demand streaming reaches us. Capped to first 200 calls.\n"
    "\t{\n"
    "\t\tstatic int s_sccCount = 0;\n"
    "\t\tif (s_sccCount < 200) {\n"
    "\t\t\tapp.DebugPrintf(\"SCC_CREATE x=%d z=%d async=%d count=%d\","
    " x, z, asyncPostProcess ? 1 : 0, s_sccCount);\n"
    "\t\t\ts_sccCount++;\n"
    "\t\t}\n"
    "\t}"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
