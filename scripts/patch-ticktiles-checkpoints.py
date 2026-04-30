#!/usr/bin/env python3
"""F3: bracket ServerLevel::tickTiles to find the null deref.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "ServerLevel.cpp"

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "TT_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    (
        "void ServerLevel::tickTiles()\n{\n"
        "\t// Index into the arrays used by the update thread\n"
        "\tint iLev = 0;",
        "void ServerLevel::tickTiles()\n{\n"
        '\tapp.DebugPrintf("TT_CKPT enter dim=%d", dimension ? ((Dimension *)dimension)->id : -99);\n'
        "\t// Index into the arrays used by the update thread\n"
        "\tint iLev = 0;",
    ),
    (
        "\tEnterCriticalSection(&m_updateCS[iLev]);",
        '\tapp.DebugPrintf("TT_CKPT iLev=%d updateCount=%d", iLev, m_updateTileCount[iLev]);\n'
        "\tEnterCriticalSection(&m_updateCS[iLev]);",
    ),
    (
        "\tLeaveCriticalSection(&m_updateCS[iLev]);\n\n"
        "\tLevel::tickTiles();",
        "\tLeaveCriticalSection(&m_updateCS[iLev]);\n"
        '\tapp.DebugPrintf("TT_CKPT before Level::tickTiles");\n\n'
        "\tLevel::tickTiles();\n"
        '\tapp.DebugPrintf("TT_CKPT after Level::tickTiles chunksToPoll=%zu", chunksToPoll.size());',
    ),
    (
        "\tint prob = 100000;",
        '\tapp.DebugPrintf("TT_CKPT before prob block");\n'
        "\tint prob = 100000;",
    ),
    (
        "\tfor (ChunkPos cp : chunksToPoll)\n\t{",
        '\tapp.DebugPrintf("TT_CKPT entering chunksToPoll loop");\n'
        "\tfor (ChunkPos cp : chunksToPoll)\n\t{",
    ),
    (
        "\t\tLevelChunk *lc = getChunk(cp.x, cp.z);",
        '\t\tapp.DebugPrintf("TT_CKPT getChunk(%d,%d) for tickClient", cp.x, cp.z);\n'
        "\t\tLevelChunk *lc = getChunk(cp.x, cp.z);\n"
        '\t\tapp.DebugPrintf("TT_CKPT lc=%p", lc);',
    ),
    (
        "\t\ttickClientSideTiles(xo, zo, lc);",
        '\t\tapp.DebugPrintf("TT_CKPT before tickClientSideTiles");\n'
        "\t\ttickClientSideTiles(xo, zo, lc);\n"
        '\t\tapp.DebugPrintf("TT_CKPT after tickClientSideTiles");',
    ),
    (
        "\t\tif (random->nextInt(prob) == 0 && isRaining() && isThundering())",
        '\t\tapp.DebugPrintf("TT_CKPT before lightning block");\n'
        "\t\tif (random->nextInt(prob) == 0 && isRaining() && isThundering())",
    ),
    (
        "\t\tif (random->nextInt(16) == 0)",
        '\t\tapp.DebugPrintf("TT_CKPT before rain/freeze block");\n'
        "\t\tif (random->nextInt(16) == 0)",
    ),
    (
        "\t\t\tint yy = getTopRainBlock(x + xo, z + zo);",
        '\t\t\tapp.DebugPrintf("TT_CKPT rain: before getTopRainBlock x=%d z=%d", x+xo, z+zo);\n'
        "\t\t\tint yy = getTopRainBlock(x + xo, z + zo);\n"
        '\t\t\tapp.DebugPrintf("TT_CKPT rain: yy=%d", yy);',
    ),
    (
        "\t\t\tif (shouldFreeze(x + xo, yy - 1, z + zo))",
        '\t\t\tapp.DebugPrintf("TT_CKPT rain: before shouldFreeze");\n'
        "\t\t\tif (shouldFreeze(x + xo, yy - 1, z + zo))",
    ),
    (
        "\t\t\tif (isRaining() && shouldSnow(x + xo, yy, z + zo))",
        '\t\t\tapp.DebugPrintf("TT_CKPT rain: before shouldSnow");\n'
        "\t\t\tif (isRaining() && shouldSnow(x + xo, yy, z + zo))",
    ),
    (
        "\t\t\tif (isRaining())\n"
        "\t\t\t{\n"
        "\t\t\t\tBiome *b = getBiome(x + xo, z + zo);",
        '\t\t\tapp.DebugPrintf("TT_CKPT rain: before isRaining/getBiome");\n'
        "\t\t\tif (isRaining())\n"
        "\t\t\t{\n"
        "\t\t\t\tBiome *b = getBiome(x + xo, z + zo);\n"
        '\t\t\t\tapp.DebugPrintf("TT_CKPT rain: biome=%p", b);',
    ),
    (
        "\t\tcheckLight(xo + random->nextInt(16), random->nextInt(128), zo + random->nextInt(16));",
        '\t\tapp.DebugPrintf("TT_CKPT before checkLight");\n'
        "\t\tcheckLight(xo + random->nextInt(16), random->nextInt(128), zo + random->nextInt(16));\n"
        '\t\tapp.DebugPrintf("TT_CKPT after checkLight");',
    ),
]

patched = src
warnings = 0
for anchor, replacement in edits:
    if anchor in patched:
        patched = patched.replace(anchor, replacement, 1)
    else:
        print(f"warning: anchor not found: {anchor[:80]!r}")
        warnings += 1

if warnings:
    sys.exit(f"{warnings} anchors missed")

TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
