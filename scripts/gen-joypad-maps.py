#!/usr/bin/env python3
"""Pull the Windows64 controller mappings out of upstream.

Windows64_Minecraft.cpp's DefineActions sets every action's buttons for the
three control schemes with InputManager.SetGameJoypadMaps. That file is the
Windows main and doesn't build on iOS, so the function body is copied into
upstream/Minecraft.Client/iOS_DefineActions.inc and Game/MCLEJoypadMaps.cpp
runs it against its own table.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Windows64" / "Windows64_Minecraft.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_DefineActions.inc"

START = "void DefineActions(void)\n{\n"

src = SRC.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
if src.count(START) != 1:
    print(f"DefineActions not found once in {SRC}")
    sys.exit(1)
begin = src.index(START) + len(START)
end = src.index("\n}\n", begin)
block = src[begin:end]
if "SetGameJoypadMaps" not in block:
    print("DefineActions has no SetGameJoypadMaps calls")
    sys.exit(1)
OUT.write_text(block + "\n", encoding="utf-8")
print(f"wrote {OUT} ({block.count('SetGameJoypadMaps')} mappings)")
