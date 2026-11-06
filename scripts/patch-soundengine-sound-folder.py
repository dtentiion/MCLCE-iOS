#!/usr/bin/env python3
"""SoundEngine::play also looks in a folder named after the sound.

Win64's media keeps some numbered sounds one folder down, named after the
sound: "ambient.cave" is ambient/cave/cave1..13.ogg, so the Win64 lookup
(ambient/cave.ogg, ambient/cave1.ogg ...) never finds them and caves are
silent. On iOS, when the sound's path is a folder, the numbered search runs
inside it.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CPP = REPO / "upstream" / "Minecraft.Client" / "Common" / "Audio" / "SoundEngine.cpp"
MARK = "MCLE_iOS_SOUND_FOLDER"

src = CPP.read_text(encoding="utf-8", errors="replace")
if MARK in src:
    print(f"already patched: {CPP}")
    sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"
anchor = nl.join([
    "\tif (!found)",
    "\t{",
    "\t\tint count = 0;",
])
if src.count(anchor) != 1:
    sys.exit(f"anchor not found once in {CPP}")
fix = nl.join([
    "#ifdef __APPLE_IOS__ // " + MARK,
    "\t// Numbered sounds kept in a folder named after the sound",
    "\tif (!found)",
    "\t{",
    "\t\tDWORD dirAttr = GetFileAttributesA(basePath);",
    "\t\tconst char *leaf = strrchr(basePath, '/');",
    "\t\tif (dirAttr != INVALID_FILE_ATTRIBUTES && (dirAttr & FILE_ATTRIBUTE_DIRECTORY) && leaf)",
    "\t\t{",
    "\t\t\tchar folderPath[256];",
    "\t\t\tsprintf_s(folderPath, \"%s%s\", basePath, leaf);",
    "\t\t\tstrcpy(basePath, folderPath);",
    "\t\t}",
    "\t}",
    "#endif",
    "",
])
src = src.replace(anchor, fix + anchor)
CPP.write_text(src, encoding="utf-8", errors="replace", newline="")
print(f"patched: {CPP}")
