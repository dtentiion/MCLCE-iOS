#!/usr/bin/env python3
"""F3 prep: bracket Tile::staticCtor with checkpoints so we can pin which
line null-derefs at addr 0x8.

Adds a checkpoint:
  - at function entry
  - after the SoundType allocations (line ~256)
  - after the tiles[] array allocation (line ~259)
  - between every tile allocation (~hundreds of lines)
The last printed checkpoint identifies the crashing line.

Idempotent.
"""
import sys
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "Tile.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "TILE_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# Anchor 1: function entry
entry_anchor = "void Tile::staticCtor()\n{\n"
entry_replacement = (
    "void Tile::staticCtor()\n{\n"
    '\tapp.DebugPrintf("TILE_CKPT enter");\n'
)
if entry_anchor not in src:
    sys.exit(f"entry anchor not found")
patched = src.replace(entry_anchor, entry_replacement, 1)

# Anchor 2: after SoundType allocs - before the tiles[] array
post_sounds_anchor = (
    "\tTile::SOUND_ANVIL = new Tile::SoundType(eMaterialSoundType_ANVIL, 0.3f, 1, "
    "eSoundType_DIG_STONE, eSoundType_RANDOM_ANVIL_LAND);"
)
post_sounds_replacement = (
    "\tTile::SOUND_ANVIL = new Tile::SoundType(eMaterialSoundType_ANVIL, 0.3f, 1, "
    "eSoundType_DIG_STONE, eSoundType_RANDOM_ANVIL_LAND);\n"
    '\tapp.DebugPrintf("TILE_CKPT sounds done");'
)
if post_sounds_anchor in patched:
    patched = patched.replace(post_sounds_anchor, post_sounds_replacement, 1)
else:
    print("warning: post_sounds anchor not found")

# Anchor 3: after tiles[] array allocation
post_tiles_anchor = "\tmemset( tiles, 0, sizeof( Tile *)*TILE_NUM_COUNT );"
post_tiles_replacement = (
    "\tmemset( tiles, 0, sizeof( Tile *)*TILE_NUM_COUNT );\n"
    '\tapp.DebugPrintf("TILE_CKPT tiles[] allocated");'
)
if post_tiles_anchor in patched:
    patched = patched.replace(post_tiles_anchor, post_tiles_replacement, 1)
else:
    print("warning: post_tiles anchor not found")

# Now insert checkpoints between every Tile:: assignment.
# Match lines starting with "\tTile::name = " in the staticCtor body.
# Track an index so each checkpoint has a unique number.
lines = patched.split("\n")
out_lines = []
in_static = False
brace_depth = 0
ckpt_idx = 0
for line in lines:
    out_lines.append(line)
    if "void Tile::staticCtor()" in line:
        in_static = True
        brace_depth = 0
        continue
    if not in_static:
        continue
    # Track braces to know when we leave the function. The function body
    # starts with { and ends when depth returns to 0.
    brace_depth += line.count("{") - line.count("}")
    if brace_depth == 0 and "}" in line:
        in_static = False
        continue
    # Only insert checkpoints after lines that look like a complete
    # Tile::xxx = ... ; assignment (ends with ;). Match those that begin
    # with "\tTile::" so we don't fire on every line.
    stripped = line.lstrip()
    if stripped.startswith("Tile::") and stripped.rstrip().endswith(";"):
        # Skip lines we already patched
        if "TILE_CKPT" in line:
            continue
        # Extract the assignee name (Tile::stone, Tile::grass, etc.) for
        # the log so we know which tile just succeeded.
        m = re.match(r"Tile::(\w+)\s*=", stripped)
        name = m.group(1) if m else f"line{ckpt_idx}"
        out_lines.append(f'\tapp.DebugPrintf("TILE_CKPT after {name}");')
        ckpt_idx += 1

print(f"inserted {ckpt_idx} per-tile checkpoints")
TARGET.write_text("\n".join(out_lines), encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
