#!/usr/bin/env python3
"""Null-guards inside LevelRenderer::renderSky + renderClouds.

Originally bracketed with per-frame LR_SKY_CKPT / LR_CLOUD_CKPT logs to
pin which deref crashed the upstream renderer-sky path. Those crashes
are fixed; the per-frame logging contributed to os_log backpressure
once worker threads landed. Keeping only the structural null guards
and the cameraTargetPlayer stand-in for mc->player.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_SKY_GUARDS" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # renderSky entry: bail early if mc/level/dimension null.
    (
        "void LevelRenderer::renderSky(float alpha)\n{\n"
        "\tif (mc->level->dimension->id == 1)",
        "void LevelRenderer::renderSky(float alpha)\n{\n"
        "\tif (!mc || !mc->level || !mc->level->dimension) return; // MCLE_SKY_GUARDS\n"
        "\tif (mc->level->dimension->id == 1)",
    ),
    # getSkyColor: guard level[playerIndex].
    (
        "\tVec3 *sc = level[playerIndex]->getSkyColor(mc->cameraTargetPlayer, alpha);",
        "\tif (!level[playerIndex]) return;\n"
        "\tVec3 *sc = level[playerIndex]->getSkyColor(mc->cameraTargetPlayer, alpha);",
    ),
    # mc->player is null in our shim; use cameraTargetPlayer as the
    # stand-in. ServerPlayer extends Entity so getPos works through
    # the Entity base.
    (
        "\tdouble yy = mc->player->getPos(alpha)->y - level[playerIndex]->getHorizonHeight();",
        "\tshared_ptr<LivingEntity> _camPlayer = mc->cameraTargetPlayer;\n"
        "\tif (!_camPlayer) return;\n"
        "\tdouble yy = _camPlayer->getPos(alpha)->y - level[playerIndex]->getHorizonHeight();",
    ),
    # renderClouds entry: bail if mc/level/dimension null.
    (
        "void LevelRenderer::renderClouds(float alpha)\n{\n"
        "\tint iTicks=ticks;",
        "void LevelRenderer::renderClouds(float alpha)\n{\n"
        "\tif (!mc || !mc->level || !mc->level->dimension) return;\n"
        "\tint iTicks=ticks;",
    ),
    # renderAdvancedClouds entry: bail if textures or camera null.
    (
        "void LevelRenderer::renderAdvancedClouds(float alpha)\n{\n"
        "\t// MGH - added",
        "void LevelRenderer::renderAdvancedClouds(float alpha)\n{\n"
        "\tif (!textures) return;\n"
        "\tif (!mc || !mc->cameraTargetPlayer) return;\n"
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
