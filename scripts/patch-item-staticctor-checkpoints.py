#!/usr/bin/env python3
"""F3 prep: bracket Item::staticCtor and Item::staticInit with per-line
checkpoints so we can pin where it null-derefs at 0x8 (same pattern as
patch-tile-staticctor-checkpoints.py).

Idempotent.
"""
import sys
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "Item.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "ITEM_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# Anchor: function entry for staticCtor
entry_anchor_sc = "void Item::staticCtor()\n{\n"
entry_replace_sc = (
    "void Item::staticCtor()\n{\n"
    '\tapp.DebugPrintf("ITEM_CKPT staticCtor enter");\n'
)
if entry_anchor_sc not in src:
    sys.exit("staticCtor entry anchor not found")
patched = src.replace(entry_anchor_sc, entry_replace_sc, 1)

# Anchor: function entry for staticInit
entry_anchor_si = "void Item::staticInit()\n{\n"
entry_replace_si = (
    "void Item::staticInit()\n{\n"
    '\tapp.DebugPrintf("ITEM_CKPT staticInit enter");\n'
)
if entry_anchor_si in patched:
    patched = patched.replace(entry_anchor_si, entry_replace_si, 1)

# Now insert checkpoints between every Item:: assignment inside both
# functions. Line based - track brace depth to know when to log.
lines = patched.split("\n")
out_lines = []
in_func = None     # "sc" or "si" or None
brace_depth = 0
for line in lines:
    out_lines.append(line)
    if "void Item::staticCtor()" in line:
        in_func = "sc"
        brace_depth = 0
        continue
    if "void Item::staticInit()" in line:
        in_func = "si"
        brace_depth = 0
        continue
    if not in_func:
        continue
    brace_depth += line.count("{") - line.count("}")
    if brace_depth == 0 and "}" in line:
        in_func = None
        continue
    stripped = line.lstrip()
    if stripped.startswith("Item::") and stripped.rstrip().endswith(";"):
        if "ITEM_CKPT" in line:
            continue
        m = re.match(r"Item::(\w+)\s*=", stripped)
        if not m:
            continue
        name = m.group(1)
        prefix = "sc" if in_func == "sc" else "si"
        out_lines.append(f'\tapp.DebugPrintf("ITEM_CKPT {prefix} after {name}");')

TARGET.write_text("\n".join(out_lines), encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
