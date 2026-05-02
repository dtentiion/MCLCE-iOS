#!/usr/bin/env python3
"""G3e-step5: bracket Level::getSkyColor body line-by-line so the next
sideload pins which exact line crashes when called from renderSky's
context (the same function works fine when called from the G1B-probe
sim path).

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "Level.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "LR_GSC" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    (
        "Vec3 *Level::getSkyColor(shared_ptr<Entity> source, float a)\n{\n"
        "\tfloat td = getTimeOfDay(a);",
        "Vec3 *Level::getSkyColor(shared_ptr<Entity> source, float a)\n{\n"
        '\tapp.DebugPrintf("LR_GSC enter this=%p source=%p a=%f", this, source.get(), (double)a);\n'
        "\tfloat td = getTimeOfDay(a);\n"
        '\tapp.DebugPrintf("LR_GSC after getTimeOfDay td=%f", (double)td);',
    ),
    (
        "\tint xx = Mth::floor(source->x);\n"
        "\tint zz = Mth::floor(source->z);\n"
        "\tBiome *biome = getBiome(xx, zz);",
        '\tapp.DebugPrintf("LR_GSC before source->x deref source=%p", source.get());\n'
        "\tint xx = Mth::floor(source->x);\n"
        "\tint zz = Mth::floor(source->z);\n"
        '\tapp.DebugPrintf("LR_GSC after source xz=(%d,%d) before getBiome", xx, zz);\n'
        "\tBiome *biome = getBiome(xx, zz);\n"
        '\tapp.DebugPrintf("LR_GSC after getBiome biome=%p", biome);',
    ),
    (
        "\tfloat temp = biome->getTemperature();\n"
        "\tint skyColor = biome->getSkyColor(temp);",
        "\tfloat temp = biome->getTemperature();\n"
        '\tapp.DebugPrintf("LR_GSC after getTemperature temp=%f", (double)temp);\n'
        "\tint skyColor = biome->getSkyColor(temp);\n"
        '\tapp.DebugPrintf("LR_GSC after biome getSkyColor=0x%08x", (unsigned)skyColor);',
    ),
    (
        "\tfloat rainLevel = getRainLevel(a);",
        '\tapp.DebugPrintf("LR_GSC before getRainLevel");\n'
        "\tfloat rainLevel = getRainLevel(a);\n"
        '\tapp.DebugPrintf("LR_GSC after getRainLevel=%f", (double)rainLevel);',
    ),
    (
        "\tfloat thunderLevel = getThunderLevel(a);",
        '\tapp.DebugPrintf("LR_GSC before getThunderLevel");\n'
        "\tfloat thunderLevel = getThunderLevel(a);\n"
        '\tapp.DebugPrintf("LR_GSC after getThunderLevel=%f", (double)thunderLevel);',
    ),
    (
        "\treturn Vec3::newTemp(r, g, b);\n"
        "}\n"
        "\n"
        "\n"
        "float Level::getTimeOfDay(float a)",
        '\tapp.DebugPrintf("LR_GSC before Vec3::newTemp r=%f g=%f b=%f", (double)r, (double)g, (double)b);\n'
        "\tVec3 *result = Vec3::newTemp(r, g, b);\n"
        '\tapp.DebugPrintf("LR_GSC after Vec3::newTemp result=%p", result);\n'
        "\treturn result;\n"
        "}\n"
        "\n"
        "\n"
        "float Level::getTimeOfDay(float a)",
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
    print(f"{warnings} anchors missed; writing partial patch")

TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
