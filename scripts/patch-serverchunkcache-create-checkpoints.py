#!/usr/bin/env python3
"""G5-step9: bracket ServerChunkCache::create() with SCC_CKPT log lines.

Preload at r=1 crashes inside create() for chunk (16,15) with SIGSEGV
at addr 0x68. The function has several candidate derefs after the
cache lookup: load(), source->getChunk(), chunk->load(), source->lightChunk(),
checkPostProcess. Logging each lets the next sideload pin which one
took the signal.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "ServerChunkCache.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "SCC_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # Function entry: log args + cache pointer.
    (
        "LevelChunk *ServerChunkCache::create(int x, int z, bool asyncPostProcess)"
        "\t// 4J - added extra parameter\n"
        "{\n"
        "\tint ix = x + XZOFFSET;",
        "LevelChunk *ServerChunkCache::create(int x, int z, bool asyncPostProcess)"
        "\t// 4J - added extra parameter\n"
        "{\n"
        '\tapp.DebugPrintf("SCC_CKPT create enter xz=%d,%d this=%p source=%p", x, z, this, source);\n'
        "\tint ix = x + XZOFFSET;",
    ),
    # Before load(x, z) - the disk load path.
    (
        "\t\tEnterCriticalSection(&m_csLoadCreate);\n"
        "        chunk = load(x, z);\n"
        "        if (chunk == nullptr)",
        "\t\tEnterCriticalSection(&m_csLoadCreate);\n"
        '\t\tapp.DebugPrintf("SCC_CKPT before load() xz=%d,%d", x, z);\n'
        "        chunk = load(x, z);\n"
        '\t\tapp.DebugPrintf("SCC_CKPT after load() chunk=%p", chunk);\n'
        "        if (chunk == nullptr)",
    ),
    # Before source->getChunk fallback.
    (
        "                chunk = source->getChunk(x, z);\n"
        "            }\n"
        "        }\n"
        "\t\tif (chunk != nullptr)\n"
        "\t\t{\n"
        "\t\t\tchunk->load();\n"
        "\t\t}",
        '\t\t\t\tapp.DebugPrintf("SCC_CKPT before source->getChunk source=%p xz=%d,%d", source, x, z);\n'
        "                chunk = source->getChunk(x, z);\n"
        '\t\t\t\tapp.DebugPrintf("SCC_CKPT after source->getChunk chunk=%p", chunk);\n'
        "            }\n"
        "        }\n"
        "\t\tif (chunk != nullptr)\n"
        "\t\t{\n"
        '\t\t\tapp.DebugPrintf("SCC_CKPT before chunk->load() chunk=%p", chunk);\n'
        "\t\t\tchunk->load();\n"
        '\t\t\tapp.DebugPrintf("SCC_CKPT after chunk->load()");\n'
        "\t\t}",
    ),
    # Before source->lightChunk.
    (
        "\t\t\t// 4J - added - this will run a recalcHeightmap if source is a randomlevelsource, which has been split out from source::getChunk so that\n"
        "\t\t\t// we are doing it after the chunk has been added to the cache - otherwise a lot of the lighting fails as lights aren't added if the chunk\n"
        "\t\t\t// they are in fail ServerChunkCache::hasChunk.\n"
        "\t\t\tsource->lightChunk(chunk);",
        "\t\t\t// 4J - added - this will run a recalcHeightmap if source is a randomlevelsource, which has been split out from source::getChunk so that\n"
        "\t\t\t// we are doing it after the chunk has been added to the cache - otherwise a lot of the lighting fails as lights aren't added if the chunk\n"
        "\t\t\t// they are in fail ServerChunkCache::hasChunk.\n"
        '\t\t\tapp.DebugPrintf("SCC_CKPT before source->lightChunk source=%p chunk=%p", source, chunk);\n'
        "\t\t\tsource->lightChunk(chunk);\n"
        '\t\t\tapp.DebugPrintf("SCC_CKPT after source->lightChunk");',
    ),
    # Before checkPostProcess in the synchronous branch.
    (
        "\t\t\telse\n"
        "\t\t\t{\n"
        "\t\t\t\tchunk->checkPostProcess(this, this, x, z);\n"
        "\t\t\t}",
        "\t\t\telse\n"
        "\t\t\t{\n"
        '\t\t\t\tapp.DebugPrintf("SCC_CKPT before checkPostProcess sync chunk=%p", chunk);\n'
        "\t\t\t\tchunk->checkPostProcess(this, this, x, z);\n"
        '\t\t\t\tapp.DebugPrintf("SCC_CKPT after checkPostProcess sync");\n'
        "\t\t\t}",
    ),
]

new_src = src
for old, new in edits:
    if old not in new_src:
        sys.exit(f"anchor not found:\n{old[:200]}")
    new_src = new_src.replace(old, new, 1)

TARGET.write_text(new_src, encoding="utf-8")
print(f"patched: {TARGET}")
