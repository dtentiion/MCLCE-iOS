#!/usr/bin/env python3
"""Make ColourTable's static name table public.

Our iOS ColourTable shim parses colours.xml at startup and needs to look
up each parsed colour name against the wchar_t array
ColourTable::ColourTableElements. That array is declared private in
upstream's header. Patch to make it public so the shim's name-to-index
lookup compiles.

m_colourValues and s_colourNamesMap stay private since we don't touch
them externally.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "Common" / "Colours" / "ColourTable.h"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_CT_PUBLIC" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "private:\n"
    "\tunsigned int m_colourValues[eMinecraftColour_COUNT];\n"
    "\n"
    "\tstatic const wchar_t *ColourTableElements[eMinecraftColour_COUNT];\n"
    "\tstatic unordered_map<wstring,eMinecraftColour> s_colourNamesMap;"
)
new = (
    "private:\n"
    "\tunsigned int m_colourValues[eMinecraftColour_COUNT];\n"
    "\n"
    "public:  // MCLE_iOS_CT_PUBLIC: shim needs the name table for XML parse\n"
    "\tstatic const wchar_t *ColourTableElements[eMinecraftColour_COUNT];\n"
    "private:\n"
    "\tstatic unordered_map<wstring,eMinecraftColour> s_colourNamesMap;"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
