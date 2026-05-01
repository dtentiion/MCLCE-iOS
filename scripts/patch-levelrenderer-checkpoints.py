#!/usr/bin/env python3
"""G2c: bracket LevelRenderer::render + renderChunks with checkpoints so
each sideload pins the exact line that null-derefs in the upstream
renderer pipeline.

Idempotent.
"""
import sys
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "LR_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # render() entry + every named-statement boundary upstream sets
    (
        "int LevelRenderer::render(shared_ptr<LivingEntity> player, int layer, double alpha, bool updateChunks)\n{\n"
        "\tint playerIndex = mc->player->GetXboxPad();",
        "int LevelRenderer::render(shared_ptr<LivingEntity> player, int layer, double alpha, bool updateChunks)\n{\n"
        '\tapp.DebugPrintf("LR_CKPT render enter mc=%p", mc);\n'
        "\tif (!mc) { return 0; }\n"
        # iOS: shim mc has player==nullptr until we have a real
        # MultiplayerLocalPlayer. Default to pad 0.
        "\tint playerIndex = (mc->player ? mc->player->GetXboxPad() : 0);\n"
        '\tapp.DebugPrintf("LR_CKPT playerIndex=%d", playerIndex);',
    ),
    (
        "\t// 4J - added - if the number of players has changed, we need to rebuild things for the new draw distance this will require\n"
        "\tif( lastPlayerCount[playerIndex] != activePlayers() )\n"
        "\t{\n"
        "\t\tallChanged();\n"
        "\t}\n"
        "\telse if (mc->options->viewDistance != lastViewDistance)\n"
        "\t{\n"
        "\t\tallChanged();\n"
        "\t}",
        '\tapp.DebugPrintf("LR_CKPT before player-count / view-distance checks");\n'
        "\t// 4J - added - if the number of players has changed, we need to rebuild things for the new draw distance this will require\n"
        "\tif( lastPlayerCount[playerIndex] != activePlayers() )\n"
        "\t{\n"
        "\t\tallChanged();\n"
        "\t}\n"
        "\telse if (mc->options && mc->options->viewDistance != lastViewDistance)\n"
        "\t{\n"
        "\t\tallChanged();\n"
        "\t}",
    ),
    (
        "\tdouble xOff = player->xOld + (player->x - player->xOld) * alpha;",
        '\tapp.DebugPrintf("LR_CKPT before player position calc");\n'
        "\tdouble xOff = player->xOld + (player->x - player->xOld) * alpha;",
    ),
    (
        "\tLighting::turnOff();",
        '\tapp.DebugPrintf("LR_CKPT before Lighting::turnOff");\n'
        "\tLighting::turnOff();\n"
        '\tapp.DebugPrintf("LR_CKPT after Lighting::turnOff");',
    ),
    (
        "\tint count = renderChunks(0, static_cast<int>(chunks[playerIndex].length), layer, alpha);",
        '\tapp.DebugPrintf("LR_CKPT before renderChunks");\n'
        "\tint count = renderChunks(0, static_cast<int>(chunks[playerIndex].length), layer, alpha);\n"
        '\tapp.DebugPrintf("LR_CKPT renderChunks count=%d", count);',
    ),
    # renderChunks body. iOS shim has mc->player == nullptr (no real
    # MultiplayerLocalPlayer constructed); drop that condition so the
    # render path can proceed to dispatch chunk geometry.
    (
        "int LevelRenderer::renderChunks(int from, int to, int layer, double alpha)\n{\n"
        "\tif (mc == nullptr || mc->player == nullptr)",
        "int LevelRenderer::renderChunks(int from, int to, int layer, double alpha)\n{\n"
        '\tapp.DebugPrintf("LR_CKPT renderChunks enter mc=%p", mc);\n'
        "\tif (mc == nullptr)",
    ),
    # iOS: shim mc->player still nullptr inside renderChunks. Default
    # playerIndex to 0 just like the outer render() path.
    (
        "\tint playerIndex = mc->player->GetXboxPad();\t// 4J added",
        "\tint playerIndex = (mc->player ? mc->player->GetXboxPad() : 0);\t// 4J added",
    ),
    (
        "\tmc->gameRenderer->turnOnLightLayer(alpha);",
        '\tapp.DebugPrintf("LR_CKPT before gameRenderer->turnOnLightLayer gameRenderer=%p", mc->gameRenderer);\n'
        "\tif (mc->gameRenderer) mc->gameRenderer->turnOnLightLayer(alpha);\n"
        '\tapp.DebugPrintf("LR_CKPT after turnOnLightLayer");',
    ),
    (
        "\tshared_ptr<LivingEntity> player = mc->cameraTargetPlayer;",
        '\tapp.DebugPrintf("LR_CKPT before cameraTargetPlayer fetch");\n'
        "\tshared_ptr<LivingEntity> player = mc->cameraTargetPlayer;\n"
        '\tapp.DebugPrintf("LR_CKPT cameraTargetPlayer=%p", player.get());',
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
