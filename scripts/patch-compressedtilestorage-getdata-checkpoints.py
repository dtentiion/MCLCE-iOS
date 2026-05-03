#!/usr/bin/env python3
"""G5-step18: log entry to CompressedTileStorage::getData (non-PSVITA)
so we see indicesAndData status + retArray.data status when called.

LCGBD_CKPT pinned crash inside lowerBlocks->getData. Body has a null
guard for indicesAndData; if that doesn't fire we crash on retArray
accesses or oob loop reads.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "CompressedTileStorage.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "CTSGD_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "void CompressedTileStorage::getData(byteArray retArray, unsigned int retOffset)\n"
    "{\n"
    "\t// Snapshot pointer to avoid race with compress() swapping indicesAndData\n"
    "\tunsigned char *localIndicesAndData = indicesAndData;\n"
    "\tif(!localIndicesAndData) return;"
)
new = (
    "void CompressedTileStorage::getData(byteArray retArray, unsigned int retOffset)\n"
    "{\n"
    '\tapp.DebugPrintf("CTSGD_CKPT enter this=%p indicesAndData=%p retArray.data=%p retArray.length=%u retOffset=%u", this, indicesAndData, retArray.data, (unsigned)retArray.length, retOffset);\n'
    "\t// Snapshot pointer to avoid race with compress() swapping indicesAndData\n"
    "\tunsigned char *localIndicesAndData = indicesAndData;\n"
    "\tif(!localIndicesAndData) {\n"
    '\t\tapp.DebugPrintf("CTSGD_CKPT bail: indicesAndData null");\n'
    "\t\treturn;\n"
    "\t}\n"
    "\tif (!retArray.data) {\n"
    '\t\tapp.DebugPrintf("CTSGD_CKPT bail: retArray.data null");\n'
    "\t\treturn;\n"
    "\t}"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")
src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
