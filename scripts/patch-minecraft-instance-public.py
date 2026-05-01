#!/usr/bin/env python3
"""G2d: expose Minecraft::m_instance so MCLEGameLoop can wire the
singleton without going through the heavy Minecraft ctor. The shim
needs Minecraft::GetInstance() to return a non-null pointer for
LevelRenderer::allChanged path to proceed.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "Minecraft.h"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")

old = "private:\n\tstatic Minecraft *m_instance;\n"
new = "public:\t// 4J iOS - shim wires this directly\n\tstatic Minecraft *m_instance;\nprivate:\n"

if new in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")

patched = src.replace(old, new, 1)
TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
