#!/usr/bin/env python3
"""F3: bracket ServerLevel::tick body to find the null deref in the very
first per-frame tick (after Player has been added).

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "ServerLevel.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "STICK_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    (
        "void ServerLevel::tick()\n{\n"
        "\tLevel::tick();",
        "void ServerLevel::tick()\n{\n"
        '\tstatic bool _tk = false; if (!_tk) { app.DebugPrintf("STICK_CKPT enter dim=%d", dimension ? ((Dimension *)dimension)->id : -99); }\n'
        "\tLevel::tick();\n"
        '\tif (!_tk) app.DebugPrintf("STICK_CKPT after Level::tick");',
    ),
    (
        "\tif (getLevelData()->isHardcore() && difficulty < 3)\n\t{",
        '\tif (!_tk) app.DebugPrintf("STICK_CKPT before isHardcore check");\n'
        "\tif (getLevelData()->isHardcore() && difficulty < 3)\n\t{",
    ),
    (
        "\tdimension->biomeSource->update();",
        '\tif (!_tk) app.DebugPrintf("STICK_CKPT before biomeSource->update dim=%p bs=%p", dimension, dimension ? ((Dimension *)dimension)->biomeSource : nullptr);\n'
        "\tdimension->biomeSource->update();\n"
        '\tif (!_tk) app.DebugPrintf("STICK_CKPT after biomeSource->update");',
    ),
    (
        "\tif (allPlayersAreSleeping())\n\t{",
        '\tif (!_tk) app.DebugPrintf("STICK_CKPT before allPlayersAreSleeping");\n'
        "\tif (allPlayersAreSleeping())\n\t{",
    ),
    (
        "\tPIXBeginNamedEvent(0,\"Mob spawner tick\");",
        '\tif (!_tk) { app.DebugPrintf("STICK_CKPT before mob spawner block"); _tk = true; }\n'
        "\tPIXBeginNamedEvent(0,\"Mob spawner tick\");",
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
    sys.exit(f"{warnings} anchors missed")

TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
