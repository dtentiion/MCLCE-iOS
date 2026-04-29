#!/usr/bin/env python3
"""Add fine-grained checkpoint prints inside NbtIo::readCompressed
around the dis.close() / dtor sequence so we see exactly which step
hangs after the NBT body parse completes.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "NbtIo.cpp"

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "NBTIO_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

anchor = (
    "DataInputStream dis = DataInputStream(in); // 4J - was new GZIPInputStream as well\n"
    "\tCompoundTag *ret = NbtIo::read((DataInput *)&dis);\n"
    "\tdis.close();\n"
    "\tMemSect(0);\n"
    "\treturn ret;"
)
replacement = (
    "DataInputStream dis = DataInputStream(in); // 4J - was new GZIPInputStream as well\n"
    '\tapp.DebugPrintf("NBTIO_CKPT before NbtIo::read");\n'
    "\tCompoundTag *ret = NbtIo::read((DataInput *)&dis);\n"
    '\tapp.DebugPrintf("NBTIO_CKPT after NbtIo::read ret=%p", ret);\n'
    "\tdis.close();\n"
    '\tapp.DebugPrintf("NBTIO_CKPT after dis.close");\n'
    "\tMemSect(0);\n"
    '\tapp.DebugPrintf("NBTIO_CKPT before return");\n'
    "\treturn ret;"
)

if anchor not in src:
    sys.exit(f"anchor not found in NbtIo.cpp")

patched = src.replace(anchor, replacement, 1)
TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
