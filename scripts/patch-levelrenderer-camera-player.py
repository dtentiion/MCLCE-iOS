#!/usr/bin/env python3
"""Don't draw the camera player's own body in first person.

renderEntities only skips the camera player when it's a LocalPlayer. Here
the camera player is the ServerPlayer, so it got drawn around the camera.
No third person view yet, so treat it as first person.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

OLD = ("if (localplayer && entity == mc->cameraTargetPlayer && "
       "!localplayer->ThirdPersonView() && !mc->cameraTargetPlayer->isSleeping()) continue;")
NEW = ("if (entity == mc->cameraTargetPlayer && (!localplayer || "
       "!localplayer->ThirdPersonView()) && !mc->cameraTargetPlayer->isSleeping()) continue;")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if NEW in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)
if OLD not in src:
    sys.exit("renderEntities camera player check not found")
TARGET.write_text(src.replace(OLD, NEW, 1), encoding="utf-8", errors="replace", newline="")
print(f"patched: {TARGET}")
