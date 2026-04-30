#!/usr/bin/env python3
"""F3: bracket ServerLevel::tick body to find the null deref. Logs every
tick (no gating) so we can see which step crashes on tick 8 specifically.

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
        '\tapp.DebugPrintf("STICK_CKPT enter dim=%d", dimension ? ((Dimension *)dimension)->id : -99);\n'
        "\tLevel::tick();\n"
        '\tapp.DebugPrintf("STICK_CKPT after Level::tick");',
    ),
    (
        "\tif (getLevelData()->isHardcore() && difficulty < 3)\n\t{",
        '\tapp.DebugPrintf("STICK_CKPT before isHardcore check");\n'
        "\tif (getLevelData()->isHardcore() && difficulty < 3)\n\t{",
    ),
    (
        "\tdimension->biomeSource->update();",
        '\tapp.DebugPrintf("STICK_CKPT before biomeSource->update");\n'
        "\tdimension->biomeSource->update();\n"
        '\tapp.DebugPrintf("STICK_CKPT after biomeSource->update");',
    ),
    (
        "\tif (allPlayersAreSleeping())\n\t{",
        '\tapp.DebugPrintf("STICK_CKPT before allPlayersAreSleeping");\n'
        "\tif (allPlayersAreSleeping())\n\t{",
    ),
    (
        "\tPIXBeginNamedEvent(0,\"Mob spawner tick\");",
        '\tapp.DebugPrintf("STICK_CKPT before mob spawner block");\n'
        "\tPIXBeginNamedEvent(0,\"Mob spawner tick\");",
    ),
    (
        "\tif (getGameRules()->getBoolean(GameRules::RULE_DOMOBSPAWNING))\n\t{",
        '\tapp.DebugPrintf("STICK_CKPT before getGameRules()");\n'
        "\tif (getGameRules()->getBoolean(GameRules::RULE_DOMOBSPAWNING))\n\t{",
    ),
    (
        "\t\tmobSpawner->tick(this, finalSpawnEnemies, finalSpawnFriendlies, finalSpawnPersistent);",
        '\t\tapp.DebugPrintf("STICK_CKPT before mobSpawner->tick");\n'
        "\t\tmobSpawner->tick(this, finalSpawnEnemies, finalSpawnFriendlies, finalSpawnPersistent);\n"
        '\t\tapp.DebugPrintf("STICK_CKPT after mobSpawner->tick");',
    ),
    (
        "\tchunkSource->tick();",
        '\tapp.DebugPrintf("STICK_CKPT before chunkSource->tick");\n'
        "\tchunkSource->tick();\n"
        '\tapp.DebugPrintf("STICK_CKPT after chunkSource->tick");',
    ),
    (
        "\tint newDark = getOldSkyDarken(1);",
        '\tapp.DebugPrintf("STICK_CKPT before getOldSkyDarken");\n'
        "\tint newDark = getOldSkyDarken(1);\n"
        '\tapp.DebugPrintf("STICK_CKPT after getOldSkyDarken=%d", newDark);',
    ),
    (
        "\tif (newDark != skyDarken)\n\t{",
        '\tapp.DebugPrintf("STICK_CKPT before sky listeners");\n'
        "\tif (newDark != skyDarken)\n\t{",
    ),
    (
        "\tint64_t time = levelData->getGameTime() + 1;",
        '\tapp.DebugPrintf("STICK_CKPT before save block");\n'
        "\tint64_t time = levelData->getGameTime() + 1;",
    ),
    (
        "\tsetGameTime(levelData->getGameTime() + 1);",
        '\tapp.DebugPrintf("STICK_CKPT before setGameTime");\n'
        "\tsetGameTime(levelData->getGameTime() + 1);\n"
        '\tapp.DebugPrintf("STICK_CKPT after setGameTime");',
    ),
    (
        "\tif (getGameRules()->getBoolean(GameRules::RULE_DAYLIGHT))\n\t{",
        '\tapp.DebugPrintf("STICK_CKPT before RULE_DAYLIGHT check");\n'
        "\tif (getGameRules()->getBoolean(GameRules::RULE_DAYLIGHT))\n\t{",
    ),
    (
        "\tPIXBeginNamedEvent(0,\"Tick pending ticks\");",
        '\tapp.DebugPrintf("STICK_CKPT before tickPendingTicks");\n'
        "\tPIXBeginNamedEvent(0,\"Tick pending ticks\");",
    ),
    (
        "\tPIXBeginNamedEvent(0,\"Tick tiles\");",
        '\tapp.DebugPrintf("STICK_CKPT before tickTiles");\n'
        "\tPIXBeginNamedEvent(0,\"Tick tiles\");",
    ),
    (
        "\tchunkMap->tick();",
        '\tapp.DebugPrintf("STICK_CKPT before chunkMap->tick");\n'
        "\tchunkMap->tick();\n"
        '\tapp.DebugPrintf("STICK_CKPT after chunkMap->tick");',
    ),
    (
        "\tPIXBeginNamedEvent(0,\"Tick villages\");",
        '\tapp.DebugPrintf("STICK_CKPT before villages->tick villages=%p siege=%p", villages.get(), villageSiege);\n'
        "\tPIXBeginNamedEvent(0,\"Tick villages\");",
    ),
    (
        "\tPIXBeginNamedEvent(0,\"Tick portal forcer\");",
        '\tapp.DebugPrintf("STICK_CKPT before portalForcer->tick");\n'
        "\tPIXBeginNamedEvent(0,\"Tick portal forcer\");",
    ),
    (
        "\tPIXBeginNamedEvent(0,\"runTileEvents\");",
        '\tapp.DebugPrintf("STICK_CKPT before runTileEvents");\n'
        "\tPIXBeginNamedEvent(0,\"runTileEvents\");",
    ),
    (
        "\trunQueuedSendTileUpdates();",
        '\tapp.DebugPrintf("STICK_CKPT before runQueuedSendTileUpdates");\n'
        "\trunQueuedSendTileUpdates();\n"
        '\tapp.DebugPrintf("STICK_CKPT tick body done");',
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
