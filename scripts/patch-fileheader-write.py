#!/usr/bin/env python3
"""Patch FileHeader::WriteHeader to do the inverse of the iOS read
patch: serialize each FileEntry as 144 bytes (Win64 disk layout) by
narrowing the 4-byte wchar_t filename slots to 2-byte uint16_t.

Without this, iOS writes the 272-byte in-memory struct over the
144-byte disk slots and corrupts adjacent file data in pvSaveMem -
which manifests as garbled region-file reads during chunk preload
right after prepareLevel succeeds.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "FileHeader.cpp"

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "IOS_FH_WRITE_DISK_SIZE" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# The exact memcpy + advance pair inside WriteHeader's loop body.
old_body = (
    "#ifdef __PSVITA__\n"
    "\t\tVirtualCopyTo((void *)headerPosition, &fileTable[i]->data, sizeof(FileEntrySaveData));\n"
    "#else\n"
    "\t\tmemcpy( (void *)headerPosition, &fileTable[i]->data, sizeof(FileEntrySaveData) );\n"
    "#endif\n"
    "\t\t//assert(numberOfBytesWritten == sizeof(FileEntrySaveData));\n"
    "\t\theaderPosition += sizeof(FileEntrySaveData);"
)
new_body = (
    "// IOS_FH_WRITE_DISK_SIZE 144\n"
    "#if defined(__APPLE_IOS__)\n"
    "\t\t{ uint8_t *_p = (uint8_t *)headerPosition;\n"
    "\t\t  for (int _j = 0; _j < 64; ++_j) {\n"
    "\t\t      uint16_t _c = (uint16_t)(fileTable[i]->data.filename[_j] & 0xFFFF);\n"
    "\t\t      _p[_j*2]     = (uint8_t)(_c & 0xFF);\n"
    "\t\t      _p[_j*2 + 1] = (uint8_t)((_c >> 8) & 0xFF);\n"
    "\t\t  }\n"
    "\t\t  _p += 128;\n"
    "\t\t  memcpy(_p, &fileTable[i]->data.length, 4); _p += 4;\n"
    "\t\t  memcpy(_p, &fileTable[i]->data.startOffset, 4); _p += 4;\n"
    "\t\t  memcpy(_p, &fileTable[i]->data.lastModifiedTime, 8); }\n"
    "\t\theaderPosition += 144;\n"
    "#elif defined(__PSVITA__)\n"
    "\t\tVirtualCopyTo((void *)headerPosition, &fileTable[i]->data, sizeof(FileEntrySaveData));\n"
    "\t\theaderPosition += sizeof(FileEntrySaveData);\n"
    "#else\n"
    "\t\tmemcpy( (void *)headerPosition, &fileTable[i]->data, sizeof(FileEntrySaveData) );\n"
    "\t\theaderPosition += sizeof(FileEntrySaveData);\n"
    "#endif"
)

if old_body not in src:
    sys.exit("WriteHeader anchor not found")

patched = src.replace(old_body, new_body, 1)

# Also patch GetFileSize() to use 144 bytes per entry on iOS (otherwise
# finalizeWrite's "are we big enough" check uses the wrong size).
gfs_old = (
    "return GetStartOfNextData() + ( sizeof(FileEntrySaveData) * static_cast<unsigned int>(fileTable.size()) );"
)
gfs_new = (
    "#if defined(__APPLE_IOS__)\n"
    "\treturn GetStartOfNextData() + ( 144 * static_cast<unsigned int>(fileTable.size()) );\n"
    "#else\n"
    "\treturn GetStartOfNextData() + ( sizeof(FileEntrySaveData) * static_cast<unsigned int>(fileTable.size()) );\n"
    "#endif"
)
if gfs_old in patched:
    patched = patched.replace(gfs_old, gfs_new, 1)

TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
