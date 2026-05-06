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

# bracket the empty check + early return path
old7 = "\tif( empty )\n\t{"
new7 = "\tapp.DebugPrintf(\"CRB_CKPT after optimization loop, empty=%d\", (int)empty);\n\tif( empty )\n\t{\n\t\tapp.DebugPrintf(\"CRB_CKPT empty path - calling setGlobalChunkFlag\");"
if old7 not in src: sys.exit("empty check anchor not found")
src = src.replace(old7, new7, 1)

# bracket the layer loop
old8 = "\tfor (int currentLayer = 0; currentLayer < 2; currentLayer++)\n\t{\n\t\tbool renderNextLayer = false;"
new8 = (
    "\tapp.DebugPrintf(\"CRB_CKPT entering currentLayer loop\");\n"
    "\tfor (int currentLayer = 0; currentLayer < 2; currentLayer++)\n\t{\n"
    "\t\tapp.DebugPrintf(\"CRB_CKPT layer=%d entry\", currentLayer);\n"
    "\t\tbool renderNextLayer = false;"
)
if old8 not in src: sys.exit("layer loop anchor not found")
src = src.replace(old8, new8, 1)

# bracket the started-block setup
old9 = "\t\t\t\t\t\t\tglNewList(lists + currentLayer, GL_COMPILE);"
new9 = (
    "\t\t\t\t\t\t\tapp.DebugPrintf(\"CRB_CKPT before glNewList lists=%d layer=%d\", lists, currentLayer);\n"
    "\t\t\t\t\t\t\tglNewList(lists + currentLayer, GL_COMPILE);\n"
    "\t\t\t\t\t\t\tapp.DebugPrintf(\"CRB_CKPT after glNewList\");"
)
if old9 not in src: sys.exit("glNewList anchor not found")
src = src.replace(old9, new9, 1)

old10 = "\t\t\t\t\t\t\tt->begin();"
new10 = (
    "\t\t\t\t\t\t\tapp.DebugPrintf(\"CRB_CKPT before t->begin t=%p\", t);\n"
    "\t\t\t\t\t\t\tt->begin();\n"
    "\t\t\t\t\t\t\tapp.DebugPrintf(\"CRB_CKPT after t->begin\");"
)
if old10 not in src: sys.exit("t->begin anchor not found")
src = src.replace(old10, new10, 1)

# bracket Tile::tiles[tileId] access + tesselateInWorld
old11 = "\t\tTile *tile = Tile::tiles[tileId];"
new11 = (
    "\t\tapp.DebugPrintf(\"CRB_CKPT before Tile::tiles[%d]\", (int)tileId);\n"
    "\t\tTile *tile = Tile::tiles[tileId];\n"
    "\t\tapp.DebugPrintf(\"CRB_CKPT tile=%p\", tile);\n"
    "\t\tif (tile == nullptr) continue;"
)
if old11 not in src: sys.exit("Tile::tiles anchor not found")
src = src.replace(old11, new11, 1)

old12 = "\t\t\t\t\trendered |= tileRenderer->tesselateInWorld(tile, x, y, z);"
new12 = (
    "\t\t\t\t\t// Inspect tile's vtable pointer. If it's null/garbage, skip.\n"
    "\t\t\t\t\t// Logs the address so we can compare across tile types.\n"
    "\t\t\t\t\tvoid *_tileVtbl = tile ? *(void**)tile : (void*)0;\n"
    "\t\t\t\t\tapp.DebugPrintf(\"CRB_CKPT pre-tess tileId=%d tile=%p vptr=%p\", (int)tileId, tile, _tileVtbl);\n"
    "\t\t\t\t\tif (_tileVtbl == nullptr) {\n"
    "\t\t\t\t\t\tapp.DebugPrintf(\"CRB_CKPT skip: null vtable for tileId=%d\", (int)tileId);\n"
    "\t\t\t\t\t} else {\n"
    "\t\t\t\t\t\trendered |= tileRenderer->tesselateInWorld(tile, x, y, z);\n"
    "\t\t\t\t\t\tapp.DebugPrintf(\"CRB_CKPT post-tess tileId=%d rendered=%d\", (int)tileId, (int)rendered);\n"
    "\t\t\t\t\t}"
)
if old12 not in src: sys.exit("tesselateInWorld anchor not found")
src = src.replace(old12, new12, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
