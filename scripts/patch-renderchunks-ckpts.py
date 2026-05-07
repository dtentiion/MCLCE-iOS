#!/usr/bin/env python3
"""bracket LevelRenderer::renderChunks to see how many chunks actually draw"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "RC_CKPT" in src:
    print("already patched"); sys.exit(0)

# Wrap the chunk render loop with stats counters and log them at end.
# Anchor: `int count = 0;` line right before the loop.
old = (
    "\tbool first = true;\n"
    "\tint count = 0;\n"
    "\tClipChunk *pClipChunk = chunks[playerIndex].data;\n"
    "\tunsigned char emptyFlag = LevelRenderer::CHUNK_FLAG_EMPTY0 << layer;\n"
    "\tfor( int i = 0; i < chunks[playerIndex].length; i++, pClipChunk++ )\n"
    "\t{\n"
    "\t\tif( !pClipChunk->visible ) continue;"
)
new = (
    "\tbool first = true;\n"
    "\tint count = 0;\n"
    "\tint rcTotal = 0, rcInvisible = 0, rcBadIdx = 0, rcEmpty = 0, rcDrawn = 0;\n"
    "\tClipChunk *pClipChunk = chunks[playerIndex].data;\n"
    "\tunsigned char emptyFlag = LevelRenderer::CHUNK_FLAG_EMPTY0 << layer;\n"
    "\tfor( int i = 0; i < chunks[playerIndex].length; i++, pClipChunk++ )\n"
    "\t{\n"
    "\t\trcTotal++;\n"
    "\t\tif( !pClipChunk->visible ) { rcInvisible++; continue; }"
)
if old not in src: sys.exit("renderChunks loop anchor not found")
src = src.replace(old, new, 1)

# Count bad-idx and empty
old2 = (
    "\t\tif( pClipChunk->globalIdx == -1 ) continue;"
)
new2 = (
    "\t\tif( pClipChunk->globalIdx == -1 ) { rcBadIdx++; continue; }"
)
if old2 not in src: sys.exit("badIdx anchor not found")
src = src.replace(old2, new2, 1)

old3 = (
    "\t\tif( ( globalChunkFlags[pClipChunk->globalIdx] & emptyFlag ) == emptyFlag ) continue;\t// Check that this particular layer isn't empty"
)
new3 = (
    "\t\tif( ( globalChunkFlags[pClipChunk->globalIdx] & emptyFlag ) == emptyFlag ) { rcEmpty++; continue; }\t// Check that this particular layer isn't empty"
)
if old3 not in src: sys.exit("empty-flag anchor not found")
src = src.replace(old3, new3, 1)

# Count drawn
old4 = (
    "\t\tif(RenderManager.CBuffCall(list, first))\n"
    "\t\t{\n"
    "\t\t\tfirst = false;\n"
    "\t\t}\n"
    "\t\tcount++;\n"
    "\t}"
)
new4 = (
    "\t\trcDrawn++;\n"
    "\t\tif(RenderManager.CBuffCall(list, first))\n"
    "\t\t{\n"
    "\t\t\tfirst = false;\n"
    "\t\t}\n"
    "\t\tcount++;\n"
    "\t}\n"
    "\tstatic int rcCallNum = 0;\n"
    "\trcCallNum++;\n"
    "\tif (rcCallNum < 20 || (rcCallNum % 120) == 0) {\n"
    "\t\tapp.DebugPrintf(\"RC_CKPT call=%d layer=%d total=%d invisible=%d badIdx=%d empty=%d drawn=%d\", rcCallNum, layer, rcTotal, rcInvisible, rcBadIdx, rcEmpty, rcDrawn);\n"
    "\t}"
)
if old4 not in src: sys.exit("drawn anchor not found")
src = src.replace(old4, new4, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
