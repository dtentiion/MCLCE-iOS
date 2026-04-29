#!/usr/bin/env python3
"""Insert checkpoint debug prints into upstream
DirectoryLevelStorage::prepareLevel so the iOS live log shows progress
through largeMapDataMappings + level.dat NBT read + LevelData ctor.

Idempotent: skips if already patched.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "DirectoryLevelStorage.cpp"

if not TARGET.exists():
    sys.exit(f"target file missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")

if "PREP_LEVEL_CKPT" in src:
    print(f"already patched, skipping: {TARGET}")
    sys.exit(0)

# Each tuple: (anchor in source, replacement). All anchors must be present.
edits = [
    # checkpoint at function entry
    (
        "LevelData *DirectoryLevelStorage::prepareLevel()\n{",
        'LevelData *DirectoryLevelStorage::prepareLevel()\n{\n'
        '\tapp.DebugPrintf("PREP_LEVEL_CKPT enter");',
    ),
    # checkpoint around dis.readFully(m_usedMappings)
    (
        "dis.readFully(m_usedMappings);",
        'app.DebugPrintf("PREP_LEVEL_CKPT before readFully len=%u", m_usedMappings.length);\n'
        '\t\t\t\tdis.readFully(m_usedMappings);\n'
        '\t\t\t\tapp.DebugPrintf("PREP_LEVEL_CKPT after readFully");',
    ),
    # checkpoint right before opening level.dat input stream
    (
        'ConsoleSaveFileInputStream fis = ConsoleSaveFileInputStream(m_saveFile, dataFile);',
        'app.DebugPrintf("PREP_LEVEL_CKPT opening level.dat stream");\n'
        '\t\tConsoleSaveFileInputStream fis = ConsoleSaveFileInputStream(m_saveFile, dataFile);\n'
        '\t\tapp.DebugPrintf("PREP_LEVEL_CKPT calling NbtIo::readCompressed");',
    ),
    # checkpoint after the NBT root parse
    (
        "CompoundTag *root = NbtIo::readCompressed(&fis);",
        'CompoundTag *root = NbtIo::readCompressed(&fis);\n'
        '\t\tapp.DebugPrintf("PREP_LEVEL_CKPT NbtIo::readCompressed returned root=%p", root);',
    ),
    # checkpoint after grabbing the Data compound
    (
        'CompoundTag *tag = root->getCompound(L"Data");',
        'CompoundTag *tag = root->getCompound(L"Data");\n'
        '\t\tapp.DebugPrintf("PREP_LEVEL_CKPT got Data compound = %p", tag);',
    ),
    # checkpoint around the LevelData ctor
    (
        "LevelData *ret = new LevelData(tag);",
        'app.DebugPrintf("PREP_LEVEL_CKPT calling LevelData ctor (tag=%p)", tag);\n'
        '\t\tLevelData *ret = new LevelData(tag);\n'
        '\t\tapp.DebugPrintf("PREP_LEVEL_CKPT LevelData ctor done = %p", ret);',
    ),
]

patched = src
for anchor, replacement in edits:
    if anchor not in patched:
        sys.exit(f"anchor not found: {anchor[:80]!r}")
    patched = patched.replace(anchor, replacement, 1)

TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patch-prepare-level-checkpoints: patched {TARGET}")
