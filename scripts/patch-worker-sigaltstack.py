#!/usr/bin/env python3
"""Install per-thread sigaltstack on chunk-rebuild worker threads.

Without this, a use-after-free crash on a worker thread (e.g. vtable
corruption of a freed LevelChunk or Tile) corrupts the stack, the
signal handler runs on that hosed stack, faults on its first call,
and the process gets SIGKILL'd without ever writing the crash trace.

Hook the very first thing rebuildChunkThreadProc does on the worker
thread, so the alt stack is in place before any real work runs.

Idempotent.
"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_WORKER_ALTSTACK" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# File-scope extern "C" decl - putting it inside the function body
# is illegal C++. Hook in right after stdafx so all upstream code
# below can see it.
decl_anchor = '#include "stdafx.h"\n'
decl_addition = (
    '#include "stdafx.h"\n'
    "#ifdef __APPLE_IOS__\n"
    "extern \"C\" void mcle_install_sig_altstack(void); // MCLE_WORKER_ALTSTACK\n"
    "#endif\n"
)
if decl_anchor not in src:
    sys.exit("include anchor not found for sigaltstack decl")
src = src.replace(decl_anchor, decl_addition, 1)

old = (
    "int LevelRenderer::rebuildChunkThreadProc(LPVOID lpParam)\n"
    "{\n"
    "\tVec3::CreateNewThreadStorage();\n"
)
new = (
    "int LevelRenderer::rebuildChunkThreadProc(LPVOID lpParam)\n"
    "{\n"
    "#ifdef __APPLE_IOS__\n"
    "\tmcle_install_sig_altstack();\n"
    "#endif\n"
    "\tVec3::CreateNewThreadStorage();\n"
)
if old not in src:
    sys.exit("rebuildChunkThreadProc anchor not found")
src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
