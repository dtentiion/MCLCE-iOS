#!/usr/bin/env python3
"""bracket ColourTable::getColour to see what id and this are at the crash"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "Common" / "Colours" / "ColourTable.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "CT_CKPT" in src:
    print("already patched"); sys.exit(0)

old = (
    "unsigned int ColourTable::getColour(eMinecraftColour id)\n"
    "{\n"
    "\treturn m_colourValues[static_cast<int>(id)];\n"
    "}"
)
new = (
    "unsigned int ColourTable::getColour(eMinecraftColour id)\n"
    "{\n"
    "\tstatic int s_logCount = 0;\n"
    "\tif (s_logCount < 5) {\n"
    "\t\tapp.DebugPrintf(\"CT_CKPT getColour this=%p id=%d\", this, (int)id);\n"
    "\t\ts_logCount++;\n"
    "\t}\n"
    "\tif (this == nullptr) {\n"
    "\t\tapp.DebugPrintf(\"CT_CKPT getColour this=null - return 0x78A7FF id=%d\", (int)id);\n"
    "\t\treturn 0x78A7FF;\n"
    "\t}\n"
    "\treturn m_colourValues[static_cast<int>(id)];\n"
    "}"
)
if old not in src: sys.exit("anchor not found")
src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
