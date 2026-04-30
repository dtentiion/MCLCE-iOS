#!/usr/bin/env python3
"""F3 prep: bracket Level::addEntity body with checkpoints to find the
null deref after Player is constructed.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "Level.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "ADDE_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    (
        "bool Level::addEntity(shared_ptr<Entity> e)\n{\n"
        "\tint xc = Mth::floor(e->x / 16);",
        "bool Level::addEntity(shared_ptr<Entity> e)\n{\n"
        '\tapp.DebugPrintf("ADDE_CKPT enter e=%p", e.get());\n'
        "\tint xc = Mth::floor(e->x / 16);",
    ),
    (
        "\tint zc = Mth::floor(e->z / 16);\n\n"
        "\tif(e == nullptr)",
        "\tint zc = Mth::floor(e->z / 16);\n"
        '\tapp.DebugPrintf("ADDE_CKPT xc=%d zc=%d", xc, zc);\n\n'
        "\tif(e == nullptr)",
    ),
    (
        "\tif (forced || hasChunk(xc, zc))\n\t{",
        '\tapp.DebugPrintf("ADDE_CKPT before hasChunk forced=%d", (int)forced);\n'
        "\tif (forced || hasChunk(xc, zc))\n\t{\n"
        '\t\tapp.DebugPrintf("ADDE_CKPT inside chunk-loaded branch");',
    ),
    (
        "\t\tif (e->instanceof(eTYPE_PLAYER))\n"
        "\t\t{\n"
        "\t\t\tshared_ptr<Player> player = dynamic_pointer_cast<Player>(e);",
        "\t\tif (e->instanceof(eTYPE_PLAYER))\n"
        "\t\t{\n"
        '\t\t\tapp.DebugPrintf("ADDE_CKPT before dynamic_pointer_cast<Player>");\n'
        "\t\t\tshared_ptr<Player> player = dynamic_pointer_cast<Player>(e);\n"
        '\t\t\tapp.DebugPrintf("ADDE_CKPT player cast=%p", player.get());',
    ),
    (
        "\t\t\tupdateSleepingPlayerList();\n\t\t}",
        '\t\t\tapp.DebugPrintf("ADDE_CKPT before updateSleepingPlayerList");\n'
        "\t\t\tupdateSleepingPlayerList();\n"
        '\t\t\tapp.DebugPrintf("ADDE_CKPT after updateSleepingPlayerList");\n'
        "\t\t}",
    ),
    (
        "\t\tMemSect(42);\n"
        "\t\tgetChunk(xc, zc)->addEntity(e);\n"
        "\t\tMemSect(0);",
        '\t\tapp.DebugPrintf("ADDE_CKPT before getChunk(%d,%d)", xc, zc);\n'
        "\t\tMemSect(42);\n"
        '\t\tauto _chunk_ckpt = getChunk(xc, zc);\n'
        '\t\tapp.DebugPrintf("ADDE_CKPT chunk=%p", _chunk_ckpt);\n'
        "\t\t_chunk_ckpt->addEntity(e);\n"
        "\t\tMemSect(0);\n"
        '\t\tapp.DebugPrintf("ADDE_CKPT chunk addEntity returned");',
    ),
    (
        "\t\tentities.push_back(e);",
        '\t\tapp.DebugPrintf("ADDE_CKPT before entities.push_back");\n'
        "\t\tentities.push_back(e);\n"
        '\t\tapp.DebugPrintf("ADDE_CKPT entities.push_back done");',
    ),
    (
        "\t\tentityAdded(e);",
        '\t\tapp.DebugPrintf("ADDE_CKPT before entityAdded");\n'
        "\t\tentityAdded(e);\n"
        '\t\tapp.DebugPrintf("ADDE_CKPT entityAdded done");',
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
