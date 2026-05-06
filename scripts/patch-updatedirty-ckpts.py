#!/usr/bin/env python3
"""line-by-line CKPTs inside LevelRenderer::updateDirtyChunks to pin crash at addr 0x8"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "WDI_CKPT" in src:
    print("already patched"); sys.exit(0)

# Bracket entry of updateDirtyChunks
old = (
    "bool LevelRenderer::updateDirtyChunks()\n"
    "{\n"
)
new = (
    "bool LevelRenderer::updateDirtyChunks()\n"
    "{\n"
    "\tapp.DebugPrintf(\"WDI_CKPT enter\");\n"
)
if old not in src:
    sys.exit("entry anchor not found")
src = src.replace(old, new, 1)

# Bracket the dirtyChunkPresent block entry
old2 = (
    "\tif( dirtyChunkPresent )\n"
    "\t{\n"
    "\t\tlastDirtyChunkFound = System::currentTimeMillis();"
)
new2 = (
    "\tapp.DebugPrintf(\"WDI_CKPT before dirtyChunkPresent block, dcp=%d\", (int)dirtyChunkPresent);\n"
    "\tif( dirtyChunkPresent )\n"
    "\t{\n"
    "\t\tapp.DebugPrintf(\"WDI_CKPT inside dirtyChunkPresent block\");\n"
    "\t\tlastDirtyChunkFound = System::currentTimeMillis();"
)
if old2 not in src:
    sys.exit("dirtyChunkPresent anchor not found")
src = src.replace(old2, new2, 1)

# Bracket the inner crash zone - use one-line anchor (single occurrence)
old3 = "Chunk *chunk = pClipChunk->chunk;"
new3 = (
    "app.DebugPrintf(\"WDI_CKPT before pClipChunk->chunk pcc=%p\", pClipChunk);\n"
    "\t\t\t\t\t\t\t\t\tChunk *chunk = pClipChunk->chunk;\n"
    "\t\t\t\t\t\t\t\t\tapp.DebugPrintf(\"WDI_CKPT chunk=%p\", chunk);\n"
    "\t\t\t\t\t\t\t\t\tif (chunk == nullptr) { continue; }"
)
if src.count(old3) != 1:
    sys.exit(f"inner crash zone anchor count={src.count(old3)} (need 1)")
src = src.replace(old3, new3, 1)

old4 = "LevelChunk *lc = level[p]->getChunkAt(chunk->x,chunk->z);"
new4 = (
    "LevelChunk *lc = level[p]->getChunkAt(chunk->x,chunk->z);\n"
    "\t\t\t\t\t\t\t\t\tapp.DebugPrintf(\"WDI_CKPT lc=%p chunk->x=%d chunk->z=%d\", lc, chunk->x, chunk->z);\n"
    "\t\t\t\t\t\t\t\t\tif (lc == nullptr) { continue; }"
)
if src.count(old4) != 1:
    sys.exit(f"getChunkAt anchor count={src.count(old4)} (need 1)")
src = src.replace(old4, new4, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
