#!/usr/bin/env python3
"""Don't draw the camera player's own body in first person.

renderEntities only skips the camera player when it's a LocalPlayer. Here
the camera player is the ServerPlayer, so it got drawn around the camera.
The third person setting lives in Game/MCLEPlayerInput.cpp instead of on a
LocalPlayer, so the check asks that.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

OLD = ("if (localplayer && entity == mc->cameraTargetPlayer && "
       "!localplayer->ThirdPersonView() && !mc->cameraTargetPlayer->isSleeping()) continue;")
NEW = ("if (entity == mc->cameraTargetPlayer && !mcle_third_person_view() && "
       "!mc->cameraTargetPlayer->isSleeping()) continue;")
ANCHOR = "void LevelRenderer::renderEntities("

src = TARGET.read_text(encoding="utf-8", errors="replace")
if NEW in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)
if OLD not in src:
    sys.exit("renderEntities camera player check not found")
if src.count(ANCHOR) != 1:
    sys.exit("renderEntities not found")
nl = "\r\n" if "\r\n" in src else "\n"
decl = "#ifdef __APPLE_IOS__" + nl + "int mcle_third_person_view();" + nl + "#endif" + nl + nl
src = src.replace(OLD, NEW, 1).replace(ANCHOR, decl + ANCHOR, 1)
TARGET.write_text(src, encoding="utf-8", errors="replace", newline="")
print(f"patched: {TARGET}")
