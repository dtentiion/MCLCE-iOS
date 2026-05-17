#!/usr/bin/env python3
"""Skip the sunrise gradient triangle fan in renderSky.

Upstream draws a 17-vertex triangle fan around the sun during the
sunrise/sunset transition window. The ring extends 120 units from
the fan center in Y and Z with a small X tilt, so the lower half
ends up well below the horizon after the orientation rotations.

In our build chunks render *after* renderSky (via replay_all_lists),
so the fan paints into the color buffer first. Any pixel below the
visible horizon line where chunks don't cover (loading gap, screen
edge, etc.) keeps the fan color - visible as a soft warm-coloured
plane that 'noclips into the ground' for the few seconds the fan
is alive.

The fan is an atmospheric polish effect, not load-bearing. Skip
it on iOS until we either implement proper depth-test against the
later-rendered chunks or build a fan that doesn't cross the
horizon.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_SKIP_SUNRISE_FAN" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = "\tfloat *c = level[playerIndex]->dimension->getSunriseColor(level[playerIndex]->getTimeOfDay(alpha), alpha);\n\tif (c != nullptr)"
new = "\tfloat *c = level[playerIndex]->dimension->getSunriseColor(level[playerIndex]->getTimeOfDay(alpha), alpha);\n\t// MCLE_iOS_SKIP_SUNRISE_FAN: ring dips below horizon and shows through chunk gaps.\n\tif (false && c != nullptr)"

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
