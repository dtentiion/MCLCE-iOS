#!/usr/bin/env python3
"""Bound m_unloadedCache so jetsam stops killing us.

Upstream ServerChunkCache::tick stashes dropped chunks into
m_unloadedCache[idx] and never frees them - Win64 perf trick that
becomes a memory leak on iOS once the player has streamed a few
hundred chunks. Cap the cache at MCLE_UNLOADED_CACHE_MAX entries
per ServerChunkCache instance via a per-instance deque of slot
indices. When the deque grows past the cap, pop the oldest and
delete that slot's LevelChunk.

Only the stash site needs bookkeeping. The eviction loop is
unconditional pop + guarded delete, so stale entries left after
rehydrate (m_unloadedCache[idx] cleared at the load site) just
get popped as no-ops.

~ServerChunkCache deletes every m_unloadedCache chunk, so the chunks
this cache evicted and hasn't freed yet go there too, along with its
slot queue. Otherwise they outlive the level, and the next world's
cache, likely at the same address, inherits the old queue.

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
    "\t\t\t\t\t\t\tstd::lock_guard<std::mutex> evictLock(s_mcleEvictMutex);\n"
    "\t\t\t\t\t\t\tconst auto _evictNow = std::chrono::steady_clock::now();\n"
    "\t\t\t\t\t\t\twhile (!s_mcleEvictFree.empty() && _evictNow - s_mcleEvictFree.front().when > std::chrono::seconds(2)) {\n"
    "\t\t\t\t\t\t\t\tdelete s_mcleEvictFree.front().chunk;\n"
    "\t\t\t\t\t\t\t\ts_mcleEvictFree.pop_front();\n"
    "\t\t\t\t\t\t\t}\n"
    "\t\t\t\t\t\t\tconstexpr size_t MCLE_UNLOADED_CACHE_MAX = 256;\n"
    "\t\t\t\t\t\t\tauto &q = s_mcleEvictQueue[this];\n"
    "\t\t\t\t\t\t\tq.push_back(idx);\n"
    "\t\t\t\t\t\t\twhile (q.size() > MCLE_UNLOADED_CACHE_MAX) {\n"
    "\t\t\t\t\t\t\t\tint oldIdx = q.front();\n"
    "\t\t\t\t\t\t\t\tq.pop_front();\n"
    "\t\t\t\t\t\t\t\tLevelChunk *dead = m_unloadedCache[oldIdx];\n"
    "\t\t\t\t\t\t\t\tif (dead && dead != chunk) {\n"
    "\t\t\t\t\t\t\t\t\t/* drop() can queue a chunk more than once. Upstream never frees\n"
    "\t\t\t\t\t\t\t\t\t   unloaded chunks so a second entry is harmless there; here it\n"
    "\t\t\t\t\t\t\t\t\t   would be freed memory, so it goes too. The front is this chunk. */\n"
    "\t\t\t\t\t\t\t\t\tm_toDrop.erase(std::remove(m_toDrop.begin() + 1, m_toDrop.end(), dead), m_toDrop.end());\n"
    "\t\t\t\t\t\t\t\t\ts_mcleEvictFree.push_back(MCLEEvictedChunk{this, dead, _evictNow});\n"
    "\t\t\t\t\t\t\t\t\tm_unloadedCache[oldIdx] = nullptr;\n"
    "\t\t\t\t\t\t\t\t\tint &n = s_mcleEvictCount[this];\n"
    "\t\t\t\t\t\t\t\t\tif (n == 0 || n % 64 == 0) {\n"
    "\t\t\t\t\t\t\t\t\t\tapp.DebugPrintf(\"MCLE_iOS_UNLOADED_EVICT cache=%p evicted=%d qsize=%zu\", this, n, q.size());\n"
    "\t\t\t\t\t\t\t\t\t}\n"
    "\t\t\t\t\t\t\t\t\tn++;\n"
    "\t\t\t\t\t\t\t\t}\n"
    "\t\t\t\t\t\t\t}\n"
    "\t\t\t\t\t\t}\n"
    "#endif\n"
)

# ~ServerChunkCache: the chunks this cache evicted and hasn't freed yet go
# with it like the m_unloadedCache ones do. Nothing else is in them by now:
# ~ServerLevel waits for the level's update thread before ~Level deletes the
# cache, and the world has stopped ticking and rebuilding chunks.
dtor_anchor = (
    "ServerChunkCache::~ServerChunkCache()\n"
    "{\n"
    "\tstorage->WaitForAll();\t\t// MGH -  added to fix crash bug 175183\n"
)
dtor_replacement = dtor_anchor + (
    "#ifdef __APPLE_IOS__\n"
    "\t/* MCLE_iOS_UNLOADED_EVICT: the chunks it evicted go with it */\n"
    "\t{\n"
    "\t\tstd::lock_guard<std::mutex> evictLock(s_mcleEvictMutex);\n"
    "\t\tfor (auto it = s_mcleEvictFree.begin(); it != s_mcleEvictFree.end();) {\n"
    "\t\t\tif (it->cache == this) {\n"
    "\t\t\t\tdelete it->chunk;\n"
    "\t\t\t\tit = s_mcleEvictFree.erase(it);\n"
    "\t\t\t} else {\n"
    "\t\t\t\t++it;\n"
    "\t\t\t}\n"
    "\t\t}\n"
    "\t\ts_mcleEvictQueue.erase(this);\n"
    "\t\ts_mcleEvictCount.erase(this);\n"
    "\t}\n"
    "#endif\n"
)

if anchor not in src:
    sys.exit(f"anchor not found in {TARGET}")
if dtor_anchor not in src:
    sys.exit(f"destructor anchor not found in {TARGET}")

# The eviction state, shared by the stash site and the destructor
include_anchor = '#include "../Minecraft.World/Tile.h"\n'
include_addition = (
    '#include "../Minecraft.World/Tile.h"\n'
    "#ifdef __APPLE_IOS__\n"
    "#include <unordered_map>\n"
    "#include <deque>\n"
    "#include <algorithm>\n"
    "#include <chrono>\n"
    "#include <mutex>\n"
    "#include <utility>\n"
    "\n"
    "/* MCLE_iOS_UNLOADED_EVICT: upstream never frees unloaded chunks, so other\n"
    "   threads (the level's update thread, chunk rebuilds) can still be inside\n"
    "   one they looked up just before it was dropped. Evicted chunks are freed\n"
    "   a while later, or with their cache. */\n"
    "namespace {\n"
    "struct MCLEEvictedChunk {\n"
    "\tServerChunkCache *cache;\n"
    "\tLevelChunk *chunk;\n"
    "\tstd::chrono::steady_clock::time_point when;\n"
    "};\n"
    "std::mutex s_mcleEvictMutex;\n"
    "std::deque<MCLEEvictedChunk> s_mcleEvictFree;\n"
    "std::unordered_map<ServerChunkCache *, std::deque<int>> s_mcleEvictQueue;\n"
    "std::unordered_map<ServerChunkCache *, int> s_mcleEvictCount;\n"
    "}\n"
    "#endif\n"
)
if include_anchor not in src:
    sys.exit(f"include anchor not found in {TARGET}")

src = src.replace(include_anchor, include_addition, 1)
src = src.replace(anchor, replacement, 1)
src = src.replace(dtor_anchor, dtor_replacement, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
