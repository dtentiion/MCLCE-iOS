#!/usr/bin/env python3
"""The player's id is the 64 bit number the Windows64 build has.

PlayerUID used to be a 16 byte struct here, written through the Sony branch
of writePlayerUID and named through the Durango one. Now it is Windows64's
ULONGLONG, so those branches are left to the consoles again: the id goes out
as a long, in a packet or in a save, and names the player's file in decimal
as it already did.

A save made while the id was 16 bytes has them in largeMapDataMappings.
DirectoryLevelStorage::prepareLevel tells by the file's length and reads
those, and the next save writes 8.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORLD = REPO / "upstream" / "Minecraft.World"
MARK = "MCLE_iOS_PLAYERUID"


def load(path):
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    return src, ("\r\n" if "\r\n" in src else "\n")


def store(path, src):
    path.write_text(src, encoding="utf-8", errors="replace", newline="")


# The stream code and the file names go back to the branch Windows64 takes
for name in ("DataOutputStream.cpp", "DataInputStream.cpp"):
    path = WORLD / name
    src, nl = load(path)
    if " || defined(__APPLE_IOS__)" in src:
        src = src.replace(" || defined(__APPLE_IOS__)", "")
        store(path, src)
        print(f"patched: {path}")
    else:
        print(f"already patched: {path}")

path = WORLD / "DirectoryLevelStorage.cpp"
src, nl = load(path)
src = src.replace("#elif defined(_DURANGO) || defined(__APPLE_IOS__)", "#elif defined(_DURANGO)")
if MARK in src:
    store(path, src)
    print(f"already patched: {path}")
    sys.exit(0)


def swap(src, old, new, count=1):
    old = old.replace("\n", nl)
    new = new.replace("\n", nl)
    if src.count(old) != count:
        sys.exit(f"anchor found {src.count(old)} times, wanted {count}: {old[:60]!r}")
    return src.replace(old, new)


# The id is printed as the number it is
src = swap(src,
           "#ifdef _WINDOWS64\n\t\t\t\tapp.DebugPrintf(\"  -- %d\\n\", playerUid);\n",
           "#if defined(_WINDOWS64) || defined(__APPLE_IOS__) // MCLE_iOS_PLAYERUID\n"
           "\t\t\t\tapp.DebugPrintf(\"  -- %d\\n\", playerUid);\n")
src = swap(src,
           "#ifdef _WINDOWS64\n\t\t\tapp.DebugPrintf(\"  -- %d\\n\", it.first);\n",
           "#if defined(_WINDOWS64) || defined(__APPLE_IOS__) // MCLE_iOS_PLAYERUID\n"
           "\t\t\tapp.DebugPrintf(\"  -- %d\\n\", it.first);\n")

src = swap(src,
           "LevelData *DirectoryLevelStorage::prepareLevel()\n{\n",
           "#if defined(__APPLE_IOS__) && defined(_LARGE_WORLDS) // MCLE_iOS_PLAYERUID\n"
           "// largeMapDataMappings is a count, then for each player its id, a count and\n"
           "// that many 12 byte mappings, then the used mappings. With 8 byte ids that\n"
           "// comes to the file's length. If it only does with 16, the save is from a\n"
           "// build that had the id 16 bytes long.\n"
           "static bool mcleMappingsLength(const byte *data, unsigned int length, int count, unsigned int idBytes, unsigned int usedBytes)\n"
           "{\n"
           "\tunsigned int pos = 4;\n"
           "\tfor (int i = 0; i < count; ++i)\n"
           "\t{\n"
           "\t\tif (pos + idBytes + 4 > length) return false;\n"
           "\t\tpos += idBytes;\n"
           "\t\tunsigned int mappings = (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];\n"
           "\t\tpos += 4;\n"
           "\t\tif (mappings > (length - pos) / 12) return false;\n"
           "\t\tpos += mappings * 12;\n"
           "\t}\n"
           "\treturn pos + usedBytes == length;\n"
           "}\n"
           "\n"
           "static bool mcleMappingsHaveOldIds(const byte *data, unsigned int length, int count, unsigned int usedBytes)\n"
           "{\n"
           "\tif (count < 0) return false;\n"
           "\treturn !mcleMappingsLength(data, length, count, 8, usedBytes) && mcleMappingsLength(data, length, count, 16, usedBytes);\n"
           "}\n"
           "\n"
           "// The number was in the first 8 of the 16 bytes, low byte first\n"
           "static PlayerUID mcleReadOldId(DataInputStream *dis)\n"
           "{\n"
           "\tPlayerUID id = 0;\n"
           "\tfor (int i = 0; i < 16; ++i)\n"
           "\t{\n"
           "\t\tPlayerUID b = static_cast<PlayerUID>(dis->readByte() & 0xff);\n"
           "\t\tif (i < 8) id |= b << (i * 8);\n"
           "\t}\n"
           "\treturn id;\n"
           "}\n"
           "#endif\n"
           "\n"
           "LevelData *DirectoryLevelStorage::prepareLevel()\n{\n")

src = swap(src,
           "\t\t\tapp.DebugPrintf(\"Loading %d mappings\\n\", count);\n"
           "\t\t\tfor(unsigned int i = 0; i < count; ++i)\n"
           "\t\t\t{\n"
           "\t\t\t\tPlayerUID playerUid = dis.readPlayerUID();\n",
           "\t\t\tapp.DebugPrintf(\"Loading %d mappings\\n\", count);\n"
           "#ifdef __APPLE_IOS__ // MCLE_iOS_PLAYERUID\n"
           "\t\t\tconst bool mcleOldIds = mcleMappingsHaveOldIds(data.data, data.length, count, m_usedMappings.length);\n"
           "#endif\n"
           "\t\t\tfor(unsigned int i = 0; i < count; ++i)\n"
           "\t\t\t{\n"
           "#ifdef __APPLE_IOS__ // MCLE_iOS_PLAYERUID\n"
           "\t\t\t\tPlayerUID playerUid = mcleOldIds ? mcleReadOldId(&dis) : dis.readPlayerUID();\n"
           "#else\n"
           "\t\t\t\tPlayerUID playerUid = dis.readPlayerUID();\n"
           "#endif\n")

store(path, src)
print(f"patched: {path}")
