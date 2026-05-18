#!/usr/bin/env python3
"""Clamp playerIndex in every LevelRenderer.cpp call site so a garbage
GetXboxPad return doesn't crash with out-of-bounds chunk array access.

Our MCLEGameLoop aliases g_player (a ServerPlayer) into mc->player
as MultiplayerLocalPlayer via reinterpret_pointer_cast. That works
for virtual dispatch but is layout-dependent for raw field reads.
GetXboxPad returns m_iPad which on a ServerPlayer-backed slot is
whatever byte happens to sit at LocalPlayer's m_iPad offset - random
heap content. Sometimes 0 (works), sometimes 1+ (chunks[1].data is
null in our single-player setup, faults the render loop at a random
address inside the player-count / view-distance block).

Replace every `int playerIndex = mc->player->GetXboxPad();` line
with a clamped variant. Single-player overworld only populates index
0 so any garbage is squashed to 0.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_CLAMP_PLAYERINDEX" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# Match every existing `int playerIndex = mc->player->GetXboxPad();`
# line (with or without the 4J comment) and append a clamp line right
# after it. Preserve indentation by capturing the leading whitespace.
import re

pattern = re.compile(
    r"^(\t+)int playerIndex = mc->player->GetXboxPad\(\);[^\n]*\n",
    re.MULTILINE,
)

def replace(m):
    indent = m.group(1)
    return (
        m.group(0)
        + f"{indent}if (playerIndex < 0 || playerIndex >= 4) playerIndex = 0; /* MCLE_iOS_CLAMP_PLAYERINDEX */\n"
    )

new_src, count = pattern.subn(replace, src)
if count == 0:
    sys.exit(f"no anchors matched in {TARGET}")

TARGET.write_text(new_src, encoding="utf-8")
print(f"patched: {TARGET} ({count} sites)")
