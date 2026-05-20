#!/usr/bin/env python3
"""Bound m_unloadedCache so jetsam stops killing us.

Upstream ServerChunkCache::tick stashes dropped chunks into
m_unloadedCache[idx] and never frees them - Win64 perf trick that
becomes a memory leak on iOS once the player has streamed a few
hundred chunks. Cap the cache at MCLE_UNLOADED_CACHE_MAX entries
per ServerChunkCache instance via a static per-instance deque of
slot indices. When the deque grows past the cap, pop the oldest
and delete that slot's LevelChunk.

Only the stash site needs bookkeeping. The eviction loop is
unconditional pop + guarded delete, so stale entries left after
rehydrate (m_unloadedCache[idx] cleared at the load site) just
get popped as no-ops.

iOS-only (__APPLE_IOS__). Win64/PSVita parity is byte-identical.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "ServerChunkCache.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_UNLOADED_EVICT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

anchor = (
    "\t\t\t\t\t\tint ix = chunk->x + XZOFFSET;\n"
    "\t\t\t\t\t\tint iz = chunk->z + XZOFFSET;\n"
    "\t\t\t\t\t\tint idx = ix * XZSIZE + iz;\n"
    "\t\t\t\t\t\tm_unloadedCache[idx] = chunk;\n"
    "\t\t\t\t\t\tcache[idx] = nullptr;\n"
)

replacement = anchor + (
    "#ifdef __APPLE_IOS__\n"
    "\t\t\t\t\t\t/* MCLE_iOS_UNLOADED_EVICT: bound m_unloadedCache, LRU evict */\n"
    "\t\t\t\t\t\t{\n"
    "\t\t\t\t\t\t\tstatic std::unordered_map<ServerChunkCache*, std::deque<int>> _evictQ;\n"
    "\t\t\t\t\t\t\tconstexpr size_t MCLE_UNLOADED_CACHE_MAX = 64;\n"
    "\t\t\t\t\t\t\tauto &q = _evictQ[this];\n"
    "\t\t\t\t\t\t\tq.push_back(idx);\n"
    "\t\t\t\t\t\t\twhile (q.size() > MCLE_UNLOADED_CACHE_MAX) {\n"
    "\t\t\t\t\t\t\t\tint oldIdx = q.front();\n"
    "\t\t\t\t\t\t\t\tq.pop_front();\n"
    "\t\t\t\t\t\t\t\tif (m_unloadedCache[oldIdx]) {\n"
    "\t\t\t\t\t\t\t\t\tdelete m_unloadedCache[oldIdx];\n"
    "\t\t\t\t\t\t\t\t\tm_unloadedCache[oldIdx] = nullptr;\n"
    "\t\t\t\t\t\t\t\t}\n"
    "\t\t\t\t\t\t\t}\n"
    "\t\t\t\t\t\t}\n"
    "#endif\n"
)

if anchor not in src:
    sys.exit(f"anchor not found in {TARGET}")

# Make sure <unordered_map> is available. <deque> is already used by the
# m_toDrop member of this class so it's pulled in transitively.
include_anchor = '#include "../Minecraft.World/Tile.h"\n'
include_addition = (
    '#include "../Minecraft.World/Tile.h"\n'
    "#ifdef __APPLE_IOS__\n"
    "#include <unordered_map>\n"
    "#include <deque>\n"
    "#endif\n"
)
if include_anchor not in src:
    sys.exit(f"include anchor not found in {TARGET}")

src = src.replace(include_anchor, include_addition, 1)
src = src.replace(anchor, replacement, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
