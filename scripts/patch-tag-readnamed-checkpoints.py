#!/usr/bin/env python3
"""Insert a log line at the top of Tag::readNamedTag so each tag type
read during NBT parsing is visible in the iOS live log. The hang in
prepareLevel happens inside NbtIo::readCompressed, and we need to see
which tag type is last before silence.

Idempotent: skips if already patched.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "Tag.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "TAG_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# Print every tag type read + the totalTagCount, plus the tag depth.
# The last line before silence is the offender.
anchor = "byte type = dis->readByte();\n\tif (type == 0) { "
replacement = (
    "byte type = dis->readByte();\n"
    '\tapp.DebugPrintf("TAG_CKPT depth=%d total=%d type=%d", depth, totalTagCount, (int)type);\n'
    "\tif (type == 0) { "
)

if anchor not in src:
    sys.exit("anchor not found in Tag.cpp")

patched = src.replace(anchor, replacement, 1)
TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
