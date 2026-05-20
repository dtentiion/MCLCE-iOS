#!/usr/bin/env python3
"""Null-guards around LevelRenderer::render + renderChunks.

Originally these sites also logged per-frame LR_CKPT lines to pin
crashes during the renderer bring-up. Those crashes are long fixed
and the per-frame logging was contributing to os_log backpressure
once worker threads landed. Keeping only the behavioural changes
(null guards, mc->player guard removed in renderChunks).

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_LR_GUARDS" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# Null-guard every `int playerIndex = mc->player->GetXboxPad();` call
# site. iOS shim's mc->player is nullptr until a real
# MultiplayerLocalPlayer is wired up; without this, render() and
# renderChunks() (and 15 other functions) all crash on entry.
playerIndex_old = "int playerIndex = mc->player->GetXboxPad();"
playerIndex_new = "int playerIndex = (mc->player ? mc->player->GetXboxPad() : 0); /* MCLE_LR_GUARDS */"
src = src.replace(playerIndex_old, playerIndex_new)

edits = [
    # render() entry: mc null guard.
    (
        "int LevelRenderer::render(shared_ptr<LivingEntity> player, int layer, double alpha, bool updateChunks)\n{\n"
        "\tint playerIndex = (mc->player ? mc->player->GetXboxPad() : 0); /* MCLE_LR_GUARDS */",
        "int LevelRenderer::render(shared_ptr<LivingEntity> player, int layer, double alpha, bool updateChunks)\n{\n"
        "\tif (!mc) { return 0; }\n"
        "\tint playerIndex = (mc->player ? mc->player->GetXboxPad() : 0); /* MCLE_LR_GUARDS */",
    ),
    # viewDistance check: guard mc->options.
    (
        "\telse if (mc->options->viewDistance != lastViewDistance)",
        "\telse if (mc->options && mc->options->viewDistance != lastViewDistance)",
    ),
    # renderChunks: drop the `mc->player == nullptr` part of the entry
    # guard so the iOS shim (no MultiplayerLocalPlayer) can dispatch.
    (
        "int LevelRenderer::renderChunks(int from, int to, int layer, double alpha)\n{\n"
        "\tif (mc == nullptr || mc->player == nullptr)",
        "int LevelRenderer::renderChunks(int from, int to, int layer, double alpha)\n{\n"
        "\tif (mc == nullptr)",
    ),
    # gameRenderer null guard.
    (
        "\tmc->gameRenderer->turnOnLightLayer(alpha);",
        "\tif (mc->gameRenderer) mc->gameRenderer->turnOnLightLayer(alpha);",
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
