#!/usr/bin/env python3
"""Add micro-checkpoints inside ServerLevel::ServerLevel ctor body so we
can see which line crashes after the Level base ctor's _init returns.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "ServerLevel.cpp"

if not TARGET.exists():
    sys.exit(f"target file missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "SLVL_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    (
        "ServerLevel::ServerLevel(MinecraftServer *server, shared_ptr<LevelStorage>levelStorage, const wstring& levelName, int dimension, LevelSettings *levelSettings) : Level(levelStorage, levelName, levelSettings, Dimension::getNew(dimension), false)\n"
        "{\n"
        "\tInitializeCriticalSection(&m_limiterCS);\n"
        "\tInitializeCriticalSection(&m_tickNextTickCS);\n"
        "\tInitializeCriticalSection(&m_csQueueSendTileUpdates);\n"
        "\tm_fallingTileCount = 0;\n"
        "\tm_primedTntCount = 0;",
        "ServerLevel::ServerLevel(MinecraftServer *server, shared_ptr<LevelStorage>levelStorage, const wstring& levelName, int dimension, LevelSettings *levelSettings) : Level(levelStorage, levelName, levelSettings, Dimension::getNew(dimension), false)\n"
        "{\n"
        '\tapp.DebugPrintf("SLVL_CKPT enter dim=%d", dimension);\n'
        "\tInitializeCriticalSection(&m_limiterCS);\n"
        "\tInitializeCriticalSection(&m_tickNextTickCS);\n"
        "\tInitializeCriticalSection(&m_csQueueSendTileUpdates);\n"
        "\tm_fallingTileCount = 0;\n"
        "\tm_primedTntCount = 0;\n"
        '\tapp.DebugPrintf("SLVL_CKPT critsections ready");',
    ),
    (
        "\t// 4J - this this used to be called in parent ctor via a virtual fn\n"
        "\tchunkSource = createChunkSource();\n"
        "\t// 4J - optimisation - keep direct reference of underlying cache here\n"
        "\tchunkSourceCache = chunkSource->getCache();\n"
        "\tchunkSourceXZSize = chunkSource->m_XZSize;",
        "\t// 4J - this this used to be called in parent ctor via a virtual fn\n"
        '\tapp.DebugPrintf("SLVL_CKPT before createChunkSource");\n'
        "\tchunkSource = createChunkSource();\n"
        '\tapp.DebugPrintf("SLVL_CKPT chunkSource=%p", chunkSource);\n'
        "\t// 4J - optimisation - keep direct reference of underlying cache here\n"
        "\tchunkSourceCache = chunkSource->getCache();\n"
        '\tapp.DebugPrintf("SLVL_CKPT chunkSourceCache=%p", chunkSourceCache);\n'
        "\tchunkSourceXZSize = chunkSource->m_XZSize;\n"
        '\tapp.DebugPrintf("SLVL_CKPT chunkSourceXZSize=%d", chunkSourceXZSize);',
    ),
    (
        "\tthis->server = server;\n"
        "\tserver->setLevel(dimension, this);",
        "\tthis->server = server;\n"
        '\tapp.DebugPrintf("SLVL_CKPT before server->setLevel");\n'
        "\tserver->setLevel(dimension, this);\n"
        '\tapp.DebugPrintf("SLVL_CKPT after server->setLevel");',
    ),
    (
        "\taddListener(new ServerLevelListener(server, this));",
        '\tapp.DebugPrintf("SLVL_CKPT before addListener(ServerLevelListener)");\n'
        "\taddListener(new ServerLevelListener(server, this));\n"
        '\tapp.DebugPrintf("SLVL_CKPT after addListener");',
    ),
    (
        "\ttracker = new EntityTracker(this);",
        '\tapp.DebugPrintf("SLVL_CKPT before EntityTracker");\n'
        "\ttracker = new EntityTracker(this);\n"
        '\tapp.DebugPrintf("SLVL_CKPT tracker=%p", tracker);',
    ),
    (
        "\tchunkMap = new PlayerChunkMap(this, dimension, server->getPlayers()->getViewDistance());",
        '\tapp.DebugPrintf("SLVL_CKPT before PlayerChunkMap");\n'
        "\tchunkMap = new PlayerChunkMap(this, dimension, server->getPlayers()->getViewDistance());\n"
        '\tapp.DebugPrintf("SLVL_CKPT chunkMap=%p", chunkMap);',
    ),
    (
        "\tmobSpawner = new MobSpawner();\n"
        "\tportalForcer = new PortalForcer(this);\n"
        "\tscoreboard = new ServerScoreboard(server);",
        '\tapp.DebugPrintf("SLVL_CKPT before MobSpawner");\n'
        "\tmobSpawner = new MobSpawner();\n"
        '\tapp.DebugPrintf("SLVL_CKPT before PortalForcer");\n'
        "\tportalForcer = new PortalForcer(this);\n"
        '\tapp.DebugPrintf("SLVL_CKPT before ServerScoreboard");\n'
        "\tscoreboard = new ServerScoreboard(server);\n"
        '\tapp.DebugPrintf("SLVL_CKPT scoreboard=%p", scoreboard);',
    ),
    (
        "\tif (!levelData->isInitialized())\n"
        "\t{\n"
        "\t\tinitializeLevel(levelSettings);\n"
        "\t\tlevelData->setInitialized(true);\n"
        "\t}",
        '\tapp.DebugPrintf("SLVL_CKPT checking levelData->isInitialized");\n'
        "\tif (!levelData->isInitialized())\n"
        "\t{\n"
        '\t\tapp.DebugPrintf("SLVL_CKPT levelData uninitialized, initializeLevel");\n'
        "\t\tinitializeLevel(levelSettings);\n"
        "\t\tlevelData->setInitialized(true);\n"
        '\t\tapp.DebugPrintf("SLVL_CKPT initializeLevel done");\n'
        "\t}",
    ),
    (
        "#ifdef _LARGE_WORLDS\n"
        "\tsaveInterval = 3;\n"
        "#else\n"
        "\tsaveInterval = 20 * 2;\n"
        "#endif\n"
        "}",
        "#ifdef _LARGE_WORLDS\n"
        "\tsaveInterval = 3;\n"
        "#else\n"
        "\tsaveInterval = 20 * 2;\n"
        "#endif\n"
        '\tapp.DebugPrintf("SLVL_CKPT ctor done dim=%d", dimension);\n'
        "}",
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
    sys.exit(f"{warnings} anchors missed; not writing")

TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
