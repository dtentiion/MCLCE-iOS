#!/usr/bin/env python3
"""G5-step12: bracket BiomeSource::getRawBiomeBlock with BS_CKPT log lines.

PH_CKPT pinned the crash inside getRawBiomeBlock. Two candidate sites:
IntCache::releaseAll (static utility) and layer->getArea(). Log around
each so the next sideload narrows further.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "BiomeSource.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "BS_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # The void overload at line 169 - that's what RandomLevelSource
    # calls. Bracket each step.
    (
        "void BiomeSource::getRawBiomeBlock(BiomeArray &biomes, int x, int z, int w, int h) const\n"
        "{\n"
        "\tIntCache::releaseAll();",
        "void BiomeSource::getRawBiomeBlock(BiomeArray &biomes, int x, int z, int w, int h) const\n"
        "{\n"
        '\tapp.DebugPrintf("BS_CKPT enter xz=%d,%d w=%d h=%d this=%p layer=%p", x, z, w, h, this, layer);\n'
        '\tapp.DebugPrintf("BS_CKPT before IntCache::releaseAll");\n'
        "\tIntCache::releaseAll();\n"
        '\tapp.DebugPrintf("BS_CKPT after IntCache::releaseAll");',
    ),
    # Before layer->getArea.
    (
        "\tintArray result = layer->getArea(x, z, w, h);\n"
        "\tfor (int i = 0; i < w * h; i++)\n"
        "\t{\n"
        "\t\tbiomes[i] = Biome::biomes[result[i]];\n"
        "#ifndef _CONTENT_PACKAGE\n"
        "\t\tif(biomes[i] == nullptr)",
        '\tapp.DebugPrintf("BS_CKPT before layer->getArea layer=%p", layer);\n'
        "\tif (!layer) {\n"
        '\t\tapp.DebugPrintf("BS_CKPT bail: layer null");\n'
        "\t\treturn;\n"
        "\t}\n"
        "\tintArray result = layer->getArea(x, z, w, h);\n"
        '\tapp.DebugPrintf("BS_CKPT after layer->getArea result.data=%p", result.data);\n'
        "\tfor (int i = 0; i < w * h; i++)\n"
        "\t{\n"
        "\t\tbiomes[i] = Biome::biomes[result[i]];\n"
        "#ifndef _CONTENT_PACKAGE\n"
        "\t\tif(biomes[i] == nullptr)",
    ),
]

new_src = src
for old, new in edits:
    if old not in new_src:
        sys.exit(f"anchor not found:\n{old[:200]}")
    new_src = new_src.replace(old, new, 1)

TARGET.write_text(new_src, encoding="utf-8")
print(f"patched: {TARGET}")
