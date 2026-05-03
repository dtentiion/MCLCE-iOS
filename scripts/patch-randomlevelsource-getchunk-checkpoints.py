#!/usr/bin/env python3
"""G5-step10: bracket RandomLevelSource::getChunk with RLS_CKPT log lines.

ServerChunkCache::create falls into source->getChunk for chunks not on
disk, and that path crashes at 0x68 for chunk (16,15). RandomLevelSource
runs the full procgen pipeline: prepareHeights, biome lookup, surface
build, then several Feature->apply calls. Logging each step pins the
offending line.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "RandomLevelSource.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "RLS_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # Function entry: log args + key member pointers.
    (
        "LevelChunk *RandomLevelSource::getChunk(int xOffs, int zOffs)\n"
        "{\n"
        "\trandom->setSeed(xOffs * 341873128712l + zOffs * 132897987541l);",
        "LevelChunk *RandomLevelSource::getChunk(int xOffs, int zOffs)\n"
        "{\n"
        '\tapp.DebugPrintf("RLS_CKPT enter xz=%d,%d this=%p random=%p level=%p", xOffs, zOffs, this, random, level);\n'
        '\tapp.DebugPrintf("RLS_CKPT before random->setSeed");\n'
        "\trandom->setSeed(xOffs * 341873128712l + zOffs * 132897987541l);\n"
        '\tapp.DebugPrintf("RLS_CKPT after random->setSeed");',
    ),
    # Before prepareHeights.
    (
        "\tprepareHeights(xOffs, zOffs, blocks);",
        '\tapp.DebugPrintf("RLS_CKPT before prepareHeights");\n'
        "\tprepareHeights(xOffs, zOffs, blocks);\n"
        '\tapp.DebugPrintf("RLS_CKPT after prepareHeights");',
    ),
    # Before biome source fetch (two derefs: level, biomeSource).
    (
        "\tBiomeArray biomes;\n"
        "\tlevel->getBiomeSource()->getBiomeBlock(biomes, xOffs * 16, zOffs * 16, 16, 16, true);",
        "\tBiomeArray biomes;\n"
        '\tapp.DebugPrintf("RLS_CKPT before getBiomeSource level=%p", level);\n'
        "\tauto *_bs = level ? level->getBiomeSource() : nullptr;\n"
        '\tapp.DebugPrintf("RLS_CKPT after getBiomeSource bs=%p", _bs);\n'
        "\tif (!_bs) {\n"
        '\t\tapp.DebugPrintf("RLS_CKPT bail: biome source null");\n'
        "\t\treturn nullptr;\n"
        "\t}\n"
        "\t_bs->getBiomeBlock(biomes, xOffs * 16, zOffs * 16, 16, 16, true);\n"
        '\tapp.DebugPrintf("RLS_CKPT after getBiomeBlock");',
    ),
    # Before buildSurfaces.
    (
        "\tbuildSurfaces(xOffs, zOffs, blocks, biomes);",
        '\tapp.DebugPrintf("RLS_CKPT before buildSurfaces");\n'
        "\tbuildSurfaces(xOffs, zOffs, blocks, biomes);\n"
        '\tapp.DebugPrintf("RLS_CKPT after buildSurfaces");',
    ),
    # Before caveFeature->apply.
    (
        "\tcaveFeature->apply(this, level, xOffs, zOffs, blocks);",
        '\tapp.DebugPrintf("RLS_CKPT before caveFeature->apply caveFeature=%p", caveFeature);\n'
        "\tif (caveFeature) caveFeature->apply(this, level, xOffs, zOffs, blocks);\n"
        '\tapp.DebugPrintf("RLS_CKPT after caveFeature->apply");',
    ),
    # Before canyonFeature->apply.
    (
        "\tcanyonFeature->apply(this, level, xOffs, zOffs, blocks);",
        '\tapp.DebugPrintf("RLS_CKPT before canyonFeature->apply canyonFeature=%p", canyonFeature);\n'
        "\tif (canyonFeature) canyonFeature->apply(this, level, xOffs, zOffs, blocks);\n"
        '\tapp.DebugPrintf("RLS_CKPT after canyonFeature->apply");',
    ),
    # Before structure features (only when generateStructures).
    (
        "\tif (generateStructures)\n"
        "\t{\n"
        "\t\tmineShaftFeature->apply(this, level, xOffs, zOffs, blocks);",
        '\tapp.DebugPrintf("RLS_CKPT generateStructures=%d", generateStructures);\n'
        "\tif (generateStructures)\n"
        "\t{\n"
        '\t\tapp.DebugPrintf("RLS_CKPT before mineShaftFeature->apply ms=%p", mineShaftFeature);\n'
        "\t\tif (mineShaftFeature) mineShaftFeature->apply(this, level, xOffs, zOffs, blocks);",
    ),
    # Before villageFeature.
    (
        "\t\tvillageFeature->apply(this, level, xOffs, zOffs, blocks);",
        '\t\tapp.DebugPrintf("RLS_CKPT before villageFeature->apply v=%p", villageFeature);\n'
        "\t\tif (villageFeature) villageFeature->apply(this, level, xOffs, zOffs, blocks);",
    ),
    # Before strongholdFeature.
    (
        "\t\tstrongholdFeature->apply(this, level, xOffs, zOffs, blocks);",
        '\t\tapp.DebugPrintf("RLS_CKPT before strongholdFeature->apply s=%p", strongholdFeature);\n'
        "\t\tif (strongholdFeature) strongholdFeature->apply(this, level, xOffs, zOffs, blocks);",
    ),
    # Before scatteredFeature.
    (
        "\t\tscatteredFeature->apply(this, level, xOffs, zOffs, blocks);\n"
        "\t}",
        '\t\tapp.DebugPrintf("RLS_CKPT before scatteredFeature->apply sc=%p", scatteredFeature);\n'
        "\t\tif (scatteredFeature) scatteredFeature->apply(this, level, xOffs, zOffs, blocks);\n"
        '\t\tapp.DebugPrintf("RLS_CKPT after structure features");\n'
        "\t}",
    ),
    # Before new LevelChunk.
    (
        "\tLevelChunk *levelChunk = new LevelChunk(level, blocks, xOffs, zOffs);",
        '\tapp.DebugPrintf("RLS_CKPT before new LevelChunk");\n'
        "\tLevelChunk *levelChunk = new LevelChunk(level, blocks, xOffs, zOffs);\n"
        '\tapp.DebugPrintf("RLS_CKPT after new LevelChunk lc=%p", levelChunk);',
    ),
]

new_src = src
for old, new in edits:
    if old not in new_src:
        sys.exit(f"anchor not found:\n{old[:200]}")
    new_src = new_src.replace(old, new, 1)

TARGET.write_text(new_src, encoding="utf-8")
print(f"patched: {TARGET}")
