#!/usr/bin/env python3
"""GameRuleManager::loadDefaultGameRules finds Tutorial.pck on iOS.

Windows64 reads Windows64Media/Tutorial/Tutorial.pck from its working
folder. The phone keeps the same media set in the app's Documents, so iOS
gets a branch that reads it from there.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "GameRules" / "GameRuleManager.cpp"

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
src = SRC.read_text(encoding="utf-8", errors="replace")
if "__APPLE_IOS__" in src:
    print(f"already patched: {SRC}")
    sys.exit(0)

ANCHOR = '#elif defined(__ORBIS__)\n\tFile packedTutorialFile(L"/app0/orbis/Tutorial/Tutorial.pck");\n'
IOS = ('#elif defined(__APPLE_IOS__)\n'
       '\tFile packedTutorialFile(convStringToWstring(string(StorageManager.GetSaveRootPath()) + "/Windows64Media/Tutorial/Tutorial.pck"));\n')
if src.count(ANCHOR) != 1:
    sys.exit(f"anchor not found once in {SRC}")
src = src.replace(ANCHOR, IOS + ANCHOR)
SRC.write_text(src, encoding="utf-8")
print(f"patched: {SRC}")
