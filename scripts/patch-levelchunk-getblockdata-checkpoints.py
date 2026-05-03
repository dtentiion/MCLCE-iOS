#!/usr/bin/env python3
"""G5-step17: bracket LevelChunk::getBlockData with LCGBD_CKPT log lines.

CHUNK_REBUILD_CKPT pinned crash at 0x0 inside lc->getBlockData(tileArray)
at Chunk.cpp:237. getBlockData body is just lowerBlocks->getData() and
optionally upperBlocks->getData(). Log both pointers and null-guard so
next sideload pins which is null.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "LevelChunk.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "LCGBD_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "void LevelChunk::getBlockData(byteArray data)\n"
    "{\n"
    "\tlowerBlocks->getData(data,0);\n"
    "\tif(data.length > Level::COMPRESSED_CHUNK_SECTION_TILES) upperBlocks->getData(data,Level::COMPRESSED_CHUNK_SECTION_TILES);\n"
    "}"
)
new = (
    "void LevelChunk::getBlockData(byteArray data)\n"
    "{\n"
    '\tapp.DebugPrintf("LCGBD_CKPT enter this=%p xz=%d,%d lowerBlocks=%p upperBlocks=%p", this, x, z, lowerBlocks, upperBlocks);\n'
    "\tif (!lowerBlocks) {\n"
    '\t\tapp.DebugPrintf("LCGBD_CKPT bail: lowerBlocks null");\n'
    "\t\treturn;\n"
    "\t}\n"
    "\tlowerBlocks->getData(data,0);\n"
    '\tapp.DebugPrintf("LCGBD_CKPT after lowerBlocks->getData");\n'
    "\tif(data.length > Level::COMPRESSED_CHUNK_SECTION_TILES) {\n"
    "\t\tif (!upperBlocks) {\n"
    '\t\t\tapp.DebugPrintf("LCGBD_CKPT bail: upperBlocks null");\n'
    "\t\t\treturn;\n"
    "\t\t}\n"
    "\t\tupperBlocks->getData(data,Level::COMPRESSED_CHUNK_SECTION_TILES);\n"
    '\t\tapp.DebugPrintf("LCGBD_CKPT after upperBlocks->getData");\n'
    "\t}\n"
    "}"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")
src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
