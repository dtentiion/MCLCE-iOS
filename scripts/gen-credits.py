#!/usr/bin/env python3
"""Pull UIScene_Credits' credit table out of upstream.

UIScene_Credits.cpp needs the Iggy UIScene to build, so its gs_aCreditDefs
initializer is copied into upstream/Minecraft.Client/iOS_CreditDefs.inc and
Game/MCLECredits.cpp compiles it.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "UI" / "UIScene_Credits.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_CreditDefs.inc"

START = "SCreditTextItemDef UIScene_Credits::gs_aCreditDefs[MAX_CREDIT_STRINGS] = \n{\n"

src = SRC.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
if src.count(START) != 1:
    print(f"credit table not found once in {SRC}")
    sys.exit(1)
begin = src.index(START) + len(START)
end = src.index("\n};\n", begin)
block = src[begin:end]
OUT.write_text(block + "\n", encoding="utf-8")
print(f"wrote {OUT} ({block.count(chr(10)) + 1} lines)")
