#!/usr/bin/env python3
"""bracket Chunk::rebuild() to pin the offset 0xe0 crash"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "Chunk.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "CRB_CKPT" in src:
    print("already patched"); sys.exit(0)

# Entry
old = "void Chunk::rebuild()\n{\n\tPIXBeginNamedEvent(0,\"Rebuilding chunk %d, %d, %d\", x, y, z);"
new = (
    "void Chunk::rebuild()\n{\n"
    "\tapp.DebugPrintf(\"CRB_CKPT enter this=%p x=%d y=%d z=%d levelRenderer=%p level=%p\", this, x, y, z, levelRenderer, level);\n"
    "\tPIXBeginNamedEvent(0,\"Rebuilding chunk %d, %d, %d\", x, y, z);"
)
if old not in src: sys.exit("entry anchor not found")
src = src.replace(old, new, 1)

# After Tesselator::getInstance()
old2 = "\tTesselator *t = Tesselator::getInstance();"
new2 = (
    "\tTesselator *t = Tesselator::getInstance();\n"
    "\tapp.DebugPrintf(\"CRB_CKPT after Tesselator::getInstance t=%p\", t);"
)
if old2 not in src: sys.exit("Tesselator anchor not found")
src = src.replace(old2, new2, 1)

# Before getGlobalIndexForChunk
old3 = "\tint lists = levelRenderer->getGlobalIndexForChunk(this->x,this->y,this->z,level) * 2;"
new3 = (
    "\tapp.DebugPrintf(\"CRB_CKPT before getGlobalIndexForChunk lr=%p level=%p\", levelRenderer, level);\n"
    "\tint lists = levelRenderer->getGlobalIndexForChunk(this->x,this->y,this->z,level) * 2;\n"
    "\tapp.DebugPrintf(\"CRB_CKPT after getGlobalIndexForChunk lists=%d\", lists);"
)
if old3 not in src: sys.exit("getGlobalIndex anchor not found")
src = src.replace(old3, new3, 1)

# Before level->getChunkAt
old4 = "\tlevel->getChunkAt(x,z)->getBlockData(tileArray);"
new4 = (
    "\tapp.DebugPrintf(\"CRB_CKPT before level->getChunkAt(x=%d, z=%d) level=%p\", x, z, level);\n"
    "\tLevelChunk *crbLc = level->getChunkAt(x, z);\n"
    "\tapp.DebugPrintf(\"CRB_CKPT crbLc=%p\", crbLc);\n"
    "\tif (crbLc) crbLc->getBlockData(tileArray);\n"
    "\tapp.DebugPrintf(\"CRB_CKPT after getBlockData\");"
)
if old4 not in src: sys.exit("getChunkAt anchor not found")
src = src.replace(old4, new4, 1)

# Before new Region/TileRenderer
old5 = "\tLevelSource *region = new Region(level, x0 - r, y0 - r, z0 - r, x1 + r, y1 + r, z1 + r, r);"
new5 = (
    "\tapp.DebugPrintf(\"CRB_CKPT before new Region\");\n"
    "\tLevelSource *region = new Region(level, x0 - r, y0 - r, z0 - r, x1 + r, y1 + r, z1 + r, r);\n"
    "\tapp.DebugPrintf(\"CRB_CKPT after new Region region=%p\", region);"
)
if old5 not in src: sys.exit("Region anchor not found")
src = src.replace(old5, new5, 1)

old6 = "\tTileRenderer *tileRenderer = new TileRenderer(region, this->x, this->y, this->z, tileIds);"
new6 = (
    "\tapp.DebugPrintf(\"CRB_CKPT before new TileRenderer\");\n"
    "\tTileRenderer *tileRenderer = new TileRenderer(region, this->x, this->y, this->z, tileIds);\n"
    "\tapp.DebugPrintf(\"CRB_CKPT after new TileRenderer tr=%p\", tileRenderer);"
)
if old6 not in src: sys.exit("TileRenderer anchor not found")
src = src.replace(old6, new6, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
