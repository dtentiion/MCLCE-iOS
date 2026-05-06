#!/usr/bin/env python3
"""line-by-line CKPTs inside LevelRenderer::updateDirtyChunks AND every
function it calls (LevelChunk::isRenderChunkEmpty, CompressedTileStorage::
isRenderChunkEmpty) - one round pins the crash regardless of which depth
it dies at."""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent / "upstream"
LR_CPP = ROOT / "Minecraft.Client" / "LevelRenderer.cpp"
LC_CPP = ROOT / "Minecraft.World" / "LevelChunk.cpp"
CTS_CPP = ROOT / "Minecraft.World" / "CompressedTileStorage.cpp"

# --- LevelRenderer.cpp ---
src = LR_CPP.read_text(encoding="utf-8", errors="replace")
if "WDI_CKPT" in src:
    print(f"already patched: {LR_CPP}")
else:
    old = "bool LevelRenderer::updateDirtyChunks()\n{\n"
    new = (
        "bool LevelRenderer::updateDirtyChunks()\n{\n"
        "\tapp.DebugPrintf(\"WDI_CKPT enter dcp=%d (forcing true)\", (int)dirtyChunkPresent);\n"
        "\t// iOS: nonStackDirtyChunksAdded push isn't sticking through whatever\n"
        "\t// initial state our shim has - force dcp every call so chunks rebuild.\n"
        "\tdirtyChunkPresent = true;\n"
    )
    if old not in src: sys.exit("entry anchor not found")
    src = src.replace(old, new, 1)

    # CKPT after queue drain showing dcp value
    old_dcp = "\t// Only bother searching round all the chunks if we have some dirty chunk(s)\n\tif( dirtyChunkPresent )"
    new_dcp = "\tapp.DebugPrintf(\"WDI_CKPT post-drain dcp=%d\", (int)dirtyChunkPresent);\n\t// Only bother searching round all the chunks if we have some dirty chunk(s)\n\tif( dirtyChunkPresent )"
    if old_dcp not in src: sys.exit("post-drain anchor not found")
    src = src.replace(old_dcp, new_dcp, 1)

    # CKPT per-player loop entry
    old_pp = "\t\tfor( int p = 0; p < XUSER_MAX_COUNT; p++ )\n\t\t{"
    new_pp = "\t\tapp.DebugPrintf(\"WDI_CKPT entering for(p) loop\");\n\t\tfor( int p = 0; p < XUSER_MAX_COUNT; p++ )\n\t\t{\n\t\t\tapp.DebugPrintf(\"WDI_CKPT p=%d player=%p chunks.data=%p level=%p len=%u expected=%d\", p, mc->localplayers[p].get(), chunks[p].data, level[p], (unsigned)chunks[p].length, xChunks*zChunks*CHUNK_Y_COUNT);"
    if old_pp not in src: sys.exit("p-loop anchor not found")
    src = src.replace(old_pp, new_pp, 1)

    # bracket pre-isRenderChunkEmpty derefs (chunk + lc accesses) with
    # null guards so we get past benign nulls
    old3 = "Chunk *chunk = pClipChunk->chunk;"
    new3 = (
        "app.DebugPrintf(\"WDI_CKPT before pClipChunk->chunk pcc=%p\", pClipChunk);\n"
        "\t\t\t\t\t\t\t\t\tChunk *chunk = pClipChunk->chunk;\n"
        "\t\t\t\t\t\t\t\t\tapp.DebugPrintf(\"WDI_CKPT chunk=%p\", chunk);\n"
        "\t\t\t\t\t\t\t\t\tif (chunk == nullptr) { continue; }"
    )
    if src.count(old3) != 1: sys.exit(f"chunk anchor count={src.count(old3)}")
    src = src.replace(old3, new3, 1)

    old4 = "LevelChunk *lc = level[p]->getChunkAt(chunk->x,chunk->z);"
    new4 = (
        "LevelChunk *lc = level[p]->getChunkAt(chunk->x,chunk->z);\n"
        "\t\t\t\t\t\t\t\t\tapp.DebugPrintf(\"WDI_CKPT lc=%p chunk->x=%d chunk->z=%d\", lc, chunk->x, chunk->z);\n"
        "\t\t\t\t\t\t\t\t\tif (lc == nullptr) { continue; }"
    )
    if src.count(old4) != 1: sys.exit(f"lc anchor count={src.count(old4)}")
    src = src.replace(old4, new4, 1)

    # CKPTs around the rebuild path - the actual chunk tessellation
    old_rb = (
        "\t\t\tchunk = it.first->chunk;\n"
    )
    new_rb = (
        "\t\t\tchunk = it.first->chunk;\n"
        "\t\t\tapp.DebugPrintf(\"WDI_CKPT rebuild iter chunk=%p index=%d\", chunk, index);\n"
        "\t\t\tif (chunk == nullptr) { ++index; continue; }\n"
    )
    if old_rb not in src: sys.exit("rebuild iter anchor not found")
    src = src.replace(old_rb, new_rb, 1)

    old_cd = "\t\t\tchunk->clearDirty();\n"
    new_cd = (
        "\t\t\tapp.DebugPrintf(\"WDI_CKPT before chunk->clearDirty\");\n"
        "\t\t\tchunk->clearDirty();\n"
        "\t\t\tapp.DebugPrintf(\"WDI_CKPT after chunk->clearDirty\");\n"
    )
    if old_cd not in src: sys.exit("clearDirty anchor not found")
    src = src.replace(old_cd, new_cd, 1)

    old_mc = "\t\t\tpermaChunk[index].makeCopyForRebuild(chunk);\n"
    new_mc = (
        "\t\t\tapp.DebugPrintf(\"WDI_CKPT before makeCopyForRebuild\");\n"
        "\t\t\tpermaChunk[index].makeCopyForRebuild(chunk);\n"
        "\t\t\tapp.DebugPrintf(\"WDI_CKPT after makeCopyForRebuild\");\n"
    )
    if old_mc not in src: sys.exit("makeCopyForRebuild anchor not found")
    src = src.replace(old_mc, new_mc, 1)

    old_rbd = "\t\t\t\tpermaChunk[index].rebuild();\n"
    new_rbd = (
        "\t\t\t\tapp.DebugPrintf(\"WDI_CKPT before permaChunk.rebuild idx=%d\", index);\n"
        "\t\t\t\tpermaChunk[index].rebuild();\n"
        "\t\t\t\tapp.DebugPrintf(\"WDI_CKPT after permaChunk.rebuild idx=%d\", index);\n"
    )
    if old_rbd not in src: sys.exit("rebuild anchor not found")
    src = src.replace(old_rbd, new_rbd, 1)

    old5 = "if( !lc->isRenderChunkEmpty(y * 16) )"
    new5 = (
        "app.DebugPrintf(\"WDI_CKPT before isRenderChunkEmpty y=%d\", y);\n"
        "\t\t\t\t\t\t\t\t\tbool lcEmpty = lc->isRenderChunkEmpty(y * 16);\n"
        "\t\t\t\t\t\t\t\t\tapp.DebugPrintf(\"WDI_CKPT after isRenderChunkEmpty empty=%d\", (int)lcEmpty);\n"
        "\t\t\t\t\t\t\t\t\tif( !lcEmpty )"
    )
    if src.count(old5) != 1: sys.exit(f"isRenderChunkEmpty anchor count={src.count(old5)}")
    src = src.replace(old5, new5, 1)

    LR_CPP.write_text(src, encoding="utf-8")
    print(f"patched {LR_CPP}")

# --- LevelChunk.cpp - bracket isRenderChunkEmpty body ---
src = LC_CPP.read_text(encoding="utf-8", errors="replace")
if "LCI_CKPT" in src:
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
        "\tapp.DebugPrintf(\"LCI_CKPT enter y=%d this=%p upper=%p lower=%p\", y, this, upperBlocks, lowerBlocks);\n"
        "\tif (!upperBlocks && !lowerBlocks) { app.DebugPrintf(\"LCI_CKPT both null - return true\"); return true; }\n"
        "\tif( isEmpty() )"
    )
    if old not in src: sys.exit("LCI anchor not found")
    src = src.replace(old, new, 1)

    old2 = "return upperBlocks->isRenderChunkEmpty( y -  Level::COMPRESSED_CHUNK_SECTION_HEIGHT );"
    new2 = (
        "if (!upperBlocks) { app.DebugPrintf(\"LCI_CKPT upperBlocks null at y=%d\", y); return true; }\n"
        "\t\tapp.DebugPrintf(\"LCI_CKPT calling upper isRenderChunkEmpty\");\n"
        "\t\treturn upperBlocks->isRenderChunkEmpty( y -  Level::COMPRESSED_CHUNK_SECTION_HEIGHT );"
    )
    if old2 not in src: sys.exit("LCI upper anchor not found")
    src = src.replace(old2, new2, 1)

    old3 = "return lowerBlocks->isRenderChunkEmpty( y );"
    new3 = (
        "if (!lowerBlocks) { app.DebugPrintf(\"LCI_CKPT lowerBlocks null at y=%d\", y); return true; }\n"
        "\t\tapp.DebugPrintf(\"LCI_CKPT calling lower isRenderChunkEmpty\");\n"
        "\t\treturn lowerBlocks->isRenderChunkEmpty( y );"
    )
    if old3 not in src: sys.exit("LCI lower anchor not found")
    src = src.replace(old3, new3, 1)

    LC_CPP.write_text(src, encoding="utf-8")
    print(f"patched {LC_CPP}")

# --- CompressedTileStorage.cpp - bracket isRenderChunkEmpty body ---
src = CTS_CPP.read_text(encoding="utf-8", errors="replace")
if "CTSI_CKPT" in src:
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
        "\tapp.DebugPrintf(\"CTSI_CKPT enter y=%d this=%p\", y, this);\n"
        "\tif (this == nullptr) { app.DebugPrintf(\"CTSI_CKPT this is null - return true\"); return true; }\n"
        "\tint block;\n"
        "\tunsigned char *localIndicesAndData = indicesAndData;"
    )
    if old not in src: sys.exit("CTSI anchor not found")
    src = src.replace(old, new, 1)
    CTS_CPP.write_text(src, encoding="utf-8")
    print(f"patched {CTS_CPP}")
