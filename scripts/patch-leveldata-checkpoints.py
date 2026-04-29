#!/usr/bin/env python3
"""Add micro-checkpoints inside LevelData(CompoundTag *) ctor so we can
see which line crashes during the second prepareLevel invocation
(called from Level::Level ctor inside ServerLevel construction).
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "LevelData.cpp"

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "LD_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    (
        "LevelData::LevelData(CompoundTag *tag)\n{\n"
        "\tseed = tag->getLong(L\"RandomSeed\");",
        "LevelData::LevelData(CompoundTag *tag)\n{\n"
        '\tapp.DebugPrintf("LD_CKPT enter tag=%p", tag);\n'
        "\tseed = tag->getLong(L\"RandomSeed\");\n"
        '\tapp.DebugPrintf("LD_CKPT seed=%lld", (long long)seed);',
    ),
    (
        "gameType = GameType::byId(tag->getInt(L\"GameType\"));",
        'app.DebugPrintf("LD_CKPT before GameType::byId");\n'
        "\tgameType = GameType::byId(tag->getInt(L\"GameType\"));\n"
        '\tapp.DebugPrintf("LD_CKPT gameType=%p", gameType);',
    ),
    (
        "levelName = tag->getString(L\"LevelName\");",
        "levelName = tag->getString(L\"LevelName\");\n"
        '\tapp.DebugPrintf("LD_CKPT got LevelName");',
    ),
    (
        "hardcore = tag->getBoolean(L\"hardcore\");",
        "hardcore = tag->getBoolean(L\"hardcore\");\n"
        '\tapp.DebugPrintf("LD_CKPT got hardcore");',
    ),
    (
        "newSeaLevel = tag->getBoolean(L\"newSeaLevel\"); // 4J added - only use new sea level for newly created maps. This read defaults to false. (sea level changes in 1.8.2)",
        "newSeaLevel = tag->getBoolean(L\"newSeaLevel\"); // 4J added - only use new sea level for newly created maps. This read defaults to false. (sea level changes in 1.8.2)\n"
        '\tapp.DebugPrintf("LD_CKPT got newSeaLevel");',
    ),
    (
        "m_xzSize = tag->getInt(L\"XZSize\");",
        'app.DebugPrintf("LD_CKPT before XZSize");\n'
        "\tm_xzSize = tag->getInt(L\"XZSize\");\n"
        '\tapp.DebugPrintf("LD_CKPT got XZSize=%d", m_xzSize);',
    ),
    (
        "m_xzSize = min(m_xzSize,LEVEL_MAX_WIDTH);",
        'app.DebugPrintf("LD_CKPT clamping xzSize");\n'
        "\tm_xzSize = min(m_xzSize,LEVEL_MAX_WIDTH);",
    ),
    (
        "app.SetGameHostOption(eGameHostOption_WorldSize, hostOptionworldSize );",
        'app.DebugPrintf("LD_CKPT before SetGameHostOption WorldSize=%d", (int)hostOptionworldSize);\n'
        "\tapp.SetGameHostOption(eGameHostOption_WorldSize, hostOptionworldSize );\n"
        '\tapp.DebugPrintf("LD_CKPT done SetGameHostOption");',
    ),
]

patched = src
for anchor, replacement in edits:
    if anchor in patched:
        patched = patched.replace(anchor, replacement, 1)
    else:
        print(f"warning: anchor not found: {anchor[:60]!r}")

TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
