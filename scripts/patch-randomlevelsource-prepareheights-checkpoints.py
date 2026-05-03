#!/usr/bin/env python3
"""G5-step11: bracket RandomLevelSource::prepareHeights with PH_CKPT log lines.

RLS_CKPT pinned the crash inside prepareHeights, between "before
prepareHeights" and "after". Three candidate lines: level->seaLevel,
level->getBiomeSource()->getRawBiomeBlock, and getHeights(). Bracket
each to pin which.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "RandomLevelSource.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "PH_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # Function entry + level->seaLevel.
    (
        "void RandomLevelSource::prepareHeights(int xOffs, int zOffs, byteArray blocks)\n"
        "{\n"
        "\tLARGE_INTEGER startTime;\n"
        "\tint xChunks = 16 / CHUNK_WIDTH;\n"
        "\tint yChunks = Level::genDepth / CHUNK_HEIGHT;\n"
        "\tint waterHeight = level->seaLevel;",
        "void RandomLevelSource::prepareHeights(int xOffs, int zOffs, byteArray blocks)\n"
        "{\n"
        '\tapp.DebugPrintf("PH_CKPT enter xz=%d,%d level=%p", xOffs, zOffs, level);\n'
        "\tLARGE_INTEGER startTime;\n"
        "\tint xChunks = 16 / CHUNK_WIDTH;\n"
        "\tint yChunks = Level::genDepth / CHUNK_HEIGHT;\n"
        '\tapp.DebugPrintf("PH_CKPT before level->seaLevel");\n'
        "\tint waterHeight = level->seaLevel;\n"
        '\tapp.DebugPrintf("PH_CKPT after level->seaLevel = %d", waterHeight);',
    ),
    # Before getRawBiomeBlock - two derefs split out so we know which is null.
    (
        "\tBiomeArray biomes;\t// 4J created locally here for thread safety, java has this as a class member\n"
        "\n"
        "\tlevel->getBiomeSource()->getRawBiomeBlock(biomes, xOffs * CHUNK_WIDTH - 2, zOffs * CHUNK_WIDTH - 2, xSize + 5, zSize + 5);",
        "\tBiomeArray biomes;\t// 4J created locally here for thread safety, java has this as a class member\n"
        "\n"
        '\tapp.DebugPrintf("PH_CKPT before getBiomeSource level=%p", level);\n'
        "\tauto *_phBs = level ? level->getBiomeSource() : nullptr;\n"
        '\tapp.DebugPrintf("PH_CKPT after getBiomeSource bs=%p", _phBs);\n'
        "\tif (!_phBs) {\n"
        '\t\tapp.DebugPrintf("PH_CKPT bail: biome source null");\n'
        "\t\treturn;\n"
        "\t}\n"
        '\tapp.DebugPrintf("PH_CKPT before getRawBiomeBlock");\n'
        "\t_phBs->getRawBiomeBlock(biomes, xOffs * CHUNK_WIDTH - 2, zOffs * CHUNK_WIDTH - 2, xSize + 5, zSize + 5);\n"
        '\tapp.DebugPrintf("PH_CKPT after getRawBiomeBlock");',
    ),
    # Before getHeights.
    (
        "\tdoubleArray buffer;\t// 4J - used to be declared with class level scope but tidying up for thread safety reasons\n"
        "\tbuffer = getHeights(buffer, xOffs * xChunks, 0, zOffs * xChunks, xSize, ySize, zSize, biomes);",
        "\tdoubleArray buffer;\t// 4J - used to be declared with class level scope but tidying up for thread safety reasons\n"
        '\tapp.DebugPrintf("PH_CKPT before getHeights");\n'
        "\tbuffer = getHeights(buffer, xOffs * xChunks, 0, zOffs * xChunks, xSize, ySize, zSize, biomes);\n"
        '\tapp.DebugPrintf("PH_CKPT after getHeights");',
    ),
]

new_src = src
for old, new in edits:
    if old not in new_src:
        sys.exit(f"anchor not found:\n{old[:200]}")
    new_src = new_src.replace(old, new, 1)

TARGET.write_text(new_src, encoding="utf-8")
print(f"patched: {TARGET}")
