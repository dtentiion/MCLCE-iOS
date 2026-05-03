#!/usr/bin/env python3
"""G5-step9: bracket Chunk::rebuild with CHUNK_REBUILD_CKPT log lines
so each sideload pins which deref crashes the upstream chunk path.

Top suspects ranked by likelihood of the 0x0 SIGSEGV (per static
analysis in scripts/_overnight_notes_g5.md):
  1. Chunk.cpp:237 - level->getChunkAt(x,z)->getBlockData(tileArray)
  2. Chunk.cpp:217 - levelRenderer->getGlobalIndexForChunk(...)
  3. Chunk.cpp:239-240 - Region / TileRenderer ctor

Idempotent. NOT YET WIRED INTO patch-upstream-stdafx.sh - to enable,
add an entry under the existing UDC_PY block. Held back until
sideload log confirms which call crashes.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "Chunk.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "CHUNK_REBUILD_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # Function entry: log this/level/levelRenderer pointers so we can
    # correlate with crash address.
    (
        "void Chunk::rebuild()\n"
        "{\n"
        "\tPIXBeginNamedEvent(0,\"Rebuilding chunk %d, %d, %d\", x, y, z);",
        "void Chunk::rebuild()\n"
        "{\n"
        '\tstatic int s_rebuildCount = 0;\n'
        '\tbool s_logRebuild = (s_rebuildCount++ < 3);\n'
        '\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT enter this=%p xyz=%d,%d,%d level=%p lr=%p call=%d", this, x, y, z, level, levelRenderer, s_rebuildCount);\n'
        "\tPIXBeginNamedEvent(0,\"Rebuilding chunk %d, %d, %d\", x, y, z);",
    ),
    # Before line 217: levelRenderer->getGlobalIndexForChunk
    (
        "\tint lists = levelRenderer->getGlobalIndexForChunk(this->x,this->y,this->z,level) * 2;\n"
        "\tlists += levelRenderer->chunkLists;",
        '\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT before getGlobalIndexForChunk lr=%p level=%p", levelRenderer, level);\n'
        "\tint lists = levelRenderer->getGlobalIndexForChunk(this->x,this->y,this->z,level) * 2;\n"
        "\tlists += levelRenderer->chunkLists;\n"
        '\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT after getGlobalIndexForChunk lists=%d", lists);',
    ),
    # Before line 237: level->getChunkAt(x,z)->getBlockData(tileArray) - TOP SUSPECT.
    # Two derefs. Insert null guard with bail so we capture which one is null.
    (
        "\tbyteArray tileArray = byteArray(tileIds, 16 * 16 * Level::maxBuildHeight);\n"
        "\tlevel->getChunkAt(x,z)->getBlockData(tileArray);",
        "\tbyteArray tileArray = byteArray(tileIds, 16 * 16 * Level::maxBuildHeight);\n"
        '\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT before getChunkAt level=%p xz=%d,%d", level, x, z);\n'
        "\t{\n"
        "\t\tauto *_lc = level ? level->getChunkAt(x,z) : nullptr;\n"
        '\t\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT after getChunkAt lc=%p", _lc);\n'
        "\t\tif (!_lc) {\n"
        '\t\t\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT bail: getChunkAt returned null");\n'
        "\t\t\tPIXEndNamedEvent();\n"
        "\t\t\treturn;\n"
        "\t\t}\n"
        "\t\t_lc->getBlockData(tileArray);\n"
        '\t\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT after getBlockData");\n'
        "\t}",
    ),
    # Before line 239-240: Region / TileRenderer ctors.
    (
        "\tLevelSource *region = new Region(level, x0 - r, y0 - r, z0 - r, x1 + r, y1 + r, z1 + r, r);\n"
        "\tTileRenderer *tileRenderer = new TileRenderer(region, this->x, this->y, this->z, tileIds);",
        '\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT before new Region");\n'
        "\tLevelSource *region = new Region(level, x0 - r, y0 - r, z0 - r, x1 + r, y1 + r, z1 + r, r);\n"
        '\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT before new TileRenderer region=%p", region);\n'
        "\tTileRenderer *tileRenderer = new TileRenderer(region, this->x, this->y, this->z, tileIds);\n"
        '\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT after new TileRenderer tr=%p", tileRenderer);',
    ),
    # End of function - if we never see this for a logged call, crash
    # is in the per-tile mesh loop.
    (
        "\tdelete tileRenderer;\n"
        "\tdelete region;",
        '\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT before delete tileRenderer/region");\n'
        "\tdelete tileRenderer;\n"
        "\tdelete region;\n"
        '\tif (s_logRebuild) app.DebugPrintf("CHUNK_REBUILD_CKPT mesh loop done, into post-process");',
    ),
]

new_src = src
for old, new in edits:
    if old not in new_src:
        sys.exit(f"anchor not found:\n{old[:200]}")
    new_src = new_src.replace(old, new, 1)

TARGET.write_text(new_src, encoding="utf-8")
print(f"patched: {TARGET}")
