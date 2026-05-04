#!/usr/bin/env python3
"""Make TexturePackRepository::DEFAULT_TEXTURE_PACK public so the iOS
boot can assign a real FolderTexturePack to it.
Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "TexturePackRepository.h"

src = TARGET.read_text(encoding="utf-8", errors="replace")
old = (
    "private:\n"
    "\tstatic TexturePack *DEFAULT_TEXTURE_PACK;"
)
new = (
    "public:\t// 4J iOS - shim assigns a real FolderTexturePack\n"
    "\tstatic TexturePack *DEFAULT_TEXTURE_PACK;\n"
    "private:"
)
if new in src:
    print("already patched")
    sys.exit(0)
if old not in src:
    sys.exit("anchor not found")
TARGET.write_text(src.replace(old, new, 1), encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
