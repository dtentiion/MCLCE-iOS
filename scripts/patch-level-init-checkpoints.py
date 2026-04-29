#!/usr/bin/env python3
"""Add micro-checkpoints inside Level::_init so we can see which line
crashes after the second prepareLevel/LevelData ctor returns.

Anchors are scoped to text unique to _init (not the upper Level::Level
ctor which shares some lines).

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "Level.cpp"

if not TARGET.exists():
    sys.exit(f"target file missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "LVL_INIT_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# anchor 1: function entry — unique signature
edits = [
    (
        "void Level::_init(shared_ptr<LevelStorage>levelStorage, const wstring& levelName, LevelSettings *levelSettings, Dimension *fixedDimension, bool doCreateChunkSource)\n{\n"
        "\t_init();\n"
        "\tthis->levelStorage = levelStorage;//shared_ptr<LevelStorage>(levelStorage);\n"
        "\tsavedDataStorage = new SavedDataStorage(levelStorage.get());",
        "void Level::_init(shared_ptr<LevelStorage>levelStorage, const wstring& levelName, LevelSettings *levelSettings, Dimension *fixedDimension, bool doCreateChunkSource)\n{\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT enter doCreate=%d", (int)doCreateChunkSource);\n'
        "\t_init();\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT after _init()");\n'
        "\tthis->levelStorage = levelStorage;//shared_ptr<LevelStorage>(levelStorage);\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT before SavedDataStorage");\n'
        "\tsavedDataStorage = new SavedDataStorage(levelStorage.get());\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT SavedDataStorage=%p", savedDataStorage);',
    ),
    # anchor 2: prepareLevel (2nd call) — unique full line in _init body
    (
        "\tlevelData = levelStorage->prepareLevel();\n"
        "\tisNew = levelData == nullptr;",
        '\tapp.DebugPrintf("LVL_INIT_CKPT before prepareLevel (2nd call)");\n'
        "\tlevelData = levelStorage->prepareLevel();\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT prepareLevel returned levelData=%p", levelData);\n'
        "\tisNew = levelData == nullptr;",
    ),
    # anchor 3: Dimension::getNew(0) — only in _init
    (
        "\telse\n"
        "\t{\n"
        "\t\tdimension = Dimension::getNew(0);\n"
        "\t}",
        "\telse\n"
        "\t{\n"
        '\t\tapp.DebugPrintf("LVL_INIT_CKPT before Dimension::getNew(0)");\n'
        "\t\tdimension = Dimension::getNew(0);\n"
        '\t\tapp.DebugPrintf("LVL_INIT_CKPT dimension=%p", dimension);\n'
        "\t}",
    ),
    # anchor 4: setLevelName branch (only in _init)
    (
        "\tif (levelData == nullptr)\n"
        "\t{\n"
        "\t\tlevelData = new LevelData(levelSettings, levelName);\n"
        "\t}\n"
        "\telse\n"
        "\t{\n"
        "\t\tlevelData->setLevelName(levelName);\n"
        "\t}",
        "\tif (levelData == nullptr)\n"
        "\t{\n"
        '\t\tapp.DebugPrintf("LVL_INIT_CKPT levelData null, building from settings");\n'
        "\t\tlevelData = new LevelData(levelSettings, levelName);\n"
        '\t\tapp.DebugPrintf("LVL_INIT_CKPT new LevelData(settings)=%p", levelData);\n'
        "\t}\n"
        "\telse\n"
        "\t{\n"
        '\t\tapp.DebugPrintf("LVL_INIT_CKPT before setLevelName");\n'
        "\t\tlevelData->setLevelName(levelName);\n"
        '\t\tapp.DebugPrintf("LVL_INIT_CKPT after setLevelName");\n'
        "\t}",
    ),
    # anchor 5: useNewSeaLevel — pair with the cast-style dimension->init line that follows in _init only
    (
        "\tif( !this->levelData->useNewSeaLevel() ) seaLevel = Level::genDepth / 2;\t\t// 4J added - sea level is one unit lower since 1.8.2, maintain older height for old levels\n"
        "\n"
        "\t((Dimension *) dimension)->init( this );",
        '\tapp.DebugPrintf("LVL_INIT_CKPT before useNewSeaLevel");\n'
        "\tif( !this->levelData->useNewSeaLevel() ) seaLevel = Level::genDepth / 2;\t\t// 4J added - sea level is one unit lower since 1.8.2, maintain older height for old levels\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT seaLevel=%d", seaLevel);\n'
        "\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT before dimension->init");\n'
        "\t((Dimension *) dimension)->init( this );\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT after dimension->init");',
    ),
    # anchor 6: doCreateChunkSource ternary — only in _init
    (
        "\tchunkSource = doCreateChunkSource ? createChunkSource() : nullptr;",
        '\tapp.DebugPrintf("LVL_INIT_CKPT before chunkSource (doCreate=%d)", (int)doCreateChunkSource);\n'
        "\tchunkSource = doCreateChunkSource ? createChunkSource() : nullptr;\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT chunkSource=%p", chunkSource);',
    ),
    # anchor 7: tail of _init (updateSkyBrightness/prepareWeather followed by closing brace
    # of _init; the upper ctor closes the same way, so include the unique cast-init line above
    # to disambiguate)
    (
        "\tupdateSkyBrightness();\n"
        "\tprepareWeather();\n"
        "\n"
        "}\n"
        "\n"
        "Level::~Level()",
        '\tapp.DebugPrintf("LVL_INIT_CKPT before updateSkyBrightness");\n'
        "\tupdateSkyBrightness();\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT before prepareWeather");\n'
        "\tprepareWeather();\n"
        '\tapp.DebugPrintf("LVL_INIT_CKPT _init done");\n'
        "\n"
        "}\n"
        "\n"
        "Level::~Level()",
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
