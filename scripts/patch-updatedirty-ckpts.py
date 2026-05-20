#!/usr/bin/env python3
"""Behavioural fixes inside LevelRenderer::updateDirtyChunks.

Originally bracketed every step with WDI_CKPT log lines while pinning
the procgen crash chain. Those crashes are fixed; the per-frame
logging contributed to os_log backpressure once worker threads
landed. Keeping only the behavioural changes:

- force dirtyChunkPresent = true on entry so chunks keep rebuilding
  even if the upstream nonStackDirtyChunksAdded path is hidden by
  our shim
- null guard pClipChunk->chunk so a benign null doesn't skip the
  whole loop
- null guard level[p]->getChunkAt result
- null guard the rebuild iter chunk pointer
"""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent / "upstream"
LR_CPP = ROOT / "Minecraft.Client" / "LevelRenderer.cpp"
LC_CPP = ROOT / "Minecraft.World" / "LevelChunk.cpp"
CTS_CPP = ROOT / "Minecraft.World" / "CompressedTileStorage.cpp"

# --- LevelRenderer.cpp ---
src = LR_CPP.read_text(encoding="utf-8", errors="replace")
if "MCLE_WDI_GUARDS" in src:
    print(f"already patched: {LR_CPP}")
else:
    old = "bool LevelRenderer::updateDirtyChunks()\n{\n"
    new = (
        "bool LevelRenderer::updateDirtyChunks()\n{\n"
        "\t// MCLE_WDI_GUARDS: nonStackDirtyChunksAdded push isn't sticking\n"
        "\t// through whatever initial state our shim has - force dcp every\n"
        "\t// call so chunks rebuild.\n"
        "\tdirtyChunkPresent = true;\n"
    )
    if old not in src: sys.exit("entry anchor not found")
    src = src.replace(old, new, 1)

    old3 = "Chunk *chunk = pClipChunk->chunk;"
    new3 = (
        "Chunk *chunk = pClipChunk->chunk;\n"
        "\t\t\t\t\t\t\t\t\tif (chunk == nullptr) { continue; }"
    )
    if src.count(old3) != 1: sys.exit(f"chunk anchor count={src.count(old3)}")
    src = src.replace(old3, new3, 1)

    old4 = "LevelChunk *lc = level[p]->getChunkAt(chunk->x,chunk->z);"
    new4 = (
        "LevelChunk *lc = level[p]->getChunkAt(chunk->x,chunk->z);\n"
        "\t\t\t\t\t\t\t\t\tif (lc == nullptr) { continue; }"
    )
    if src.count(old4) != 1: sys.exit(f"lc anchor count={src.count(old4)}")
    src = src.replace(old4, new4, 1)

    old_rb = "\t\t\tchunk = it.first->chunk;\n"
    new_rb = (
        "\t\t\tchunk = it.first->chunk;\n"
        "\t\t\tif (chunk == nullptr) { ++index; continue; }\n"
    )
    if old_rb not in src: sys.exit("rebuild iter anchor not found")
    src = src.replace(old_rb, new_rb, 1)

    LR_CPP.write_text(src, encoding="utf-8")
    print(f"patched {LR_CPP}")

# --- LevelChunk.cpp - null guard isRenderChunkEmpty body ---
src = LC_CPP.read_text(encoding="utf-8", errors="replace")
if "MCLE_LCI_GUARDS" in src:
    print(f"already patched: {LC_CPP}")
else:
    old = (
        "bool LevelChunk::isRenderChunkEmpty(int y)\n"
        "{\n"
        "\tif( isEmpty() )"
    )
    new = (
        "bool LevelChunk::isRenderChunkEmpty(int y)\n"
        "{\n"
        "\tif (!upperBlocks && !lowerBlocks) { return true; } // MCLE_LCI_GUARDS\n"
        "\tif( isEmpty() )"
    )
    if old not in src: sys.exit("LCI anchor not found")
    src = src.replace(old, new, 1)

    old2 = "return upperBlocks->isRenderChunkEmpty( y -  Level::COMPRESSED_CHUNK_SECTION_HEIGHT );"
    new2 = (
        "if (!upperBlocks) return true;\n"
        "\t\treturn upperBlocks->isRenderChunkEmpty( y -  Level::COMPRESSED_CHUNK_SECTION_HEIGHT );"
    )
    if old2 not in src: sys.exit("LCI upper anchor not found")
    src = src.replace(old2, new2, 1)

    old3 = "return lowerBlocks->isRenderChunkEmpty( y );"
    new3 = (
        "if (!lowerBlocks) return true;\n"
        "\t\treturn lowerBlocks->isRenderChunkEmpty( y );"
    )
    if old3 not in src: sys.exit("LCI lower anchor not found")
    src = src.replace(old3, new3, 1)

    LC_CPP.write_text(src, encoding="utf-8")
    print(f"patched {LC_CPP}")

# --- CompressedTileStorage.cpp - null this guard ---
src = CTS_CPP.read_text(encoding="utf-8", errors="replace")
if "MCLE_CTSI_GUARDS" in src:
    print(f"already patched: {CTS_CPP}")
else:
    old = (
        "bool CompressedTileStorage::isRenderChunkEmpty(int y)\t// y == 0, 16, 32... 112 (representing a 16 byte range)\n"
        "{\n"
        "\tint block;\n"
        "\tunsigned char *localIndicesAndData = indicesAndData;"
    )
    new = (
        "bool CompressedTileStorage::isRenderChunkEmpty(int y)\t// y == 0, 16, 32... 112 (representing a 16 byte range)\n"
        "{\n"
        "\tif (this == nullptr) return true; // MCLE_CTSI_GUARDS\n"
        "\tint block;\n"
        "\tunsigned char *localIndicesAndData = indicesAndData;"
    )
    if old not in src: sys.exit("CTSI anchor not found")
    src = src.replace(old, new, 1)
    CTS_CPP.write_text(src, encoding="utf-8")
    print(f"patched {CTS_CPP}")
