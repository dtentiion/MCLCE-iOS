#!/usr/bin/env python3
"""G3e-step2: bracket LevelRenderer::renderSky + renderClouds with
LR_SKY_CKPT log lines so each sideload pins which deref crashes the
upstream renderer-sky path.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "LR_SKY_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # renderSky entry: log mc and the level/dimension state so the
    # crash address can be correlated with which deref bit.
    (
        "void LevelRenderer::renderSky(float alpha)\n{\n"
        "\tif (mc->level->dimension->id == 1)",
        "void LevelRenderer::renderSky(float alpha)\n{\n"
        '\tapp.DebugPrintf("LR_SKY_CKPT renderSky enter mc=%p level=%p", mc, mc ? (void*)mc->level : nullptr);\n'
        "\tif (!mc || !mc->level || !mc->level->dimension) {\n"
        '\t\tapp.DebugPrintf("LR_SKY_CKPT renderSky bail: mc/level/dimension null");\n'
        "\t\treturn;\n"
        "\t}\n"
        '\tapp.DebugPrintf("LR_SKY_CKPT before dimension->id check, dim=%p", mc->level->dimension);\n'
        "\tif (mc->level->dimension->id == 1)",
    ),
    # Before isNaturalDimension virtual call.
    (
        "\tif (!mc->level->dimension->isNaturalDimension()) return;\n"
        "\n"
        "\tglDisable(GL_TEXTURE_2D);",
        '\tapp.DebugPrintf("LR_SKY_CKPT before isNaturalDimension");\n'
        "\tif (!mc->level->dimension->isNaturalDimension()) return;\n"
        '\tapp.DebugPrintf("LR_SKY_CKPT after isNaturalDimension");\n'
        "\n"
        "\tglDisable(GL_TEXTURE_2D);",
    ),
    # Before getSkyColor virtual call.
    (
        "\tVec3 *sc = level[playerIndex]->getSkyColor(mc->cameraTargetPlayer, alpha);",
        '\tapp.DebugPrintf("LR_SKY_CKPT before getSkyColor level[%d]=%p", playerIndex, level[playerIndex]);\n'
        "\tif (!level[playerIndex]) return;\n"
        "\tVec3 *sc = level[playerIndex]->getSkyColor(mc->cameraTargetPlayer, alpha);\n"
        '\tapp.DebugPrintf("LR_SKY_CKPT after getSkyColor sc=%p", sc);',
    ),
    # Before sky list dispatch.
    (
        "\tglEnable(GL_FOG);\n"
        "\tglColor3f(sr, sg, sb);\n"
        "\tglCallList(skyList);",
        '\tapp.DebugPrintf("LR_SKY_CKPT before glCallList(skyList=%d)", skyList);\n'
        "\tglEnable(GL_FOG);\n"
        "\tglColor3f(sr, sg, sb);\n"
        "\tglCallList(skyList);\n"
        '\tapp.DebugPrintf("LR_SKY_CKPT after glCallList(skyList)");',
    ),
    # renderClouds entry. fancyGraphics=true in our shim so this calls
    # renderAdvancedClouds which derefs textures-> bindTexture etc.
    (
        "void LevelRenderer::renderClouds(float alpha)\n{\n"
        "\tint iTicks=ticks;",
        "void LevelRenderer::renderClouds(float alpha)\n{\n"
        '\tapp.DebugPrintf("LR_CLOUD_CKPT renderClouds enter mc=%p", mc);\n'
        "\tif (!mc || !mc->level || !mc->level->dimension) {\n"
        '\t\tapp.DebugPrintf("LR_CLOUD_CKPT renderClouds bail: mc/level/dimension null");\n'
        "\t\treturn;\n"
        "\t}\n"
        "\tint iTicks=ticks;",
    ),
    # renderAdvancedClouds entry. Derefs textures-> first thing.
    (
        "void LevelRenderer::renderAdvancedClouds(float alpha)\n{\n"
        "\t// MGH - added",
        "void LevelRenderer::renderAdvancedClouds(float alpha)\n{\n"
        '\tapp.DebugPrintf("LR_CLOUD_CKPT renderAdvancedClouds enter textures=%p", textures);\n'
        "\tif (!textures) {\n"
        '\t\tapp.DebugPrintf("LR_CLOUD_CKPT renderAdvancedClouds bail: textures null");\n'
        "\t\treturn;\n"
        "\t}\n"
        "\t// MGH - added",
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
