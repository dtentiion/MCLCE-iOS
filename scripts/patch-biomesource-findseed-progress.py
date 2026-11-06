#!/usr/bin/env python3
"""BiomeSource::findSeed when there is no progress renderer.

A new world with an empty seed has findSeed pick one, and findSeed shows
its search on Minecraft's progressRenderer. The port starts the server
before it has a Minecraft, and the one it makes later has no renderer,
so on iOS the search runs without it. The loading screen has its own text
for this stage.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CPP = REPO / "upstream" / "Minecraft.World" / "BiomeSource.cpp"

if not CPP.exists():
    sys.exit(f"missing: {CPP}")
src = CPP.read_text(encoding="utf-8", errors="replace")
if "mcprogress != nullptr" in src:
    print(f"already patched: {CPP}")
    sys.exit(0)

STAGE = ("\tProgressRenderer *mcprogress = Minecraft::GetInstance()->progressRenderer;\n"
         "\tmcprogress->progressStage(IDS_PROGRESS_NEW_WORLD_SEED);\n")
STAGE_IOS = ("#ifdef __APPLE_IOS__\n"
             "\tProgressRenderer *mcprogress = Minecraft::GetInstance() != nullptr ? Minecraft::GetInstance()->progressRenderer : nullptr;\n"
             "\tif (mcprogress != nullptr) mcprogress->progressStage(IDS_PROGRESS_NEW_WORLD_SEED);\n"
             "#else\n" + STAGE + "#endif\n")
PERCENT = "\t\t\t\tmcprogress->progressStagePercentage( tryCount % 100 );\n"
PERCENT_IOS = ("#ifdef __APPLE_IOS__\n"
               "\t\t\t\tif (mcprogress != nullptr)\n"
               "#endif\n" + PERCENT)

for old, new in ((STAGE, STAGE_IOS), (PERCENT, PERCENT_IOS)):
    if src.count(old) != 1:
        sys.exit(f"anchor not found once in {CPP}: {old!r}")
    src = src.replace(old, new)
CPP.write_text(src, encoding="utf-8")
print(f"patched: {CPP}")
