#!/usr/bin/env python3
"""Per-phase log inside ServerChunkCache::create so we can see where it
crashes when loading chunks beyond the r=3 preload ring.

Phases:
- entry (already logged via SCC_CREATE)
- post-load (after load() returned)
- post-source (after source->getChunk if load returned null)
- post-lightChunk
- post-checkPostProcess
- exit

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "ServerChunkCache.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_SCC_PHASES" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    (
        "        chunk = load(x, z);\n        if (chunk == nullptr)",
        "        chunk = load(x, z);\n"
        "        { static int c = 0; if (c<200) { app.DebugPrintf(\"SCC_PHASE x=%d z=%d post-load chunk=%p\", x, z, (void*)chunk); c++; } }\n"
        "        if (chunk == nullptr)",
    ),
    (
        "                chunk = source->getChunk(x, z);\n            }",
        "                chunk = source->getChunk(x, z);\n"
        "                { static int c = 0; if (c<200) { app.DebugPrintf(\"SCC_PHASE x=%d z=%d post-source chunk=%p\", x, z, (void*)chunk); c++; } }\n"
        "            }",
    ),
    (
        "\t\t\tsource->lightChunk(chunk);",
        "\t\t\tsource->lightChunk(chunk);\n"
        "\t\t\t{ static int c = 0; if (c<200) { app.DebugPrintf(\"SCC_PHASE x=%d z=%d post-lightChunk\", x, z); c++; } }",
    ),
    (
        "\t\t\t\tchunk->checkPostProcess(this, this, x, z);",
        "\t\t\t\tchunk->checkPostProcess(this, this, x, z);\n"
        "\t\t\t\t{ static int c = 0; if (c<200) { app.DebugPrintf(\"SCC_PHASE x=%d z=%d post-checkPostProcess\", x, z); c++; } }",
    ),
]

count = 0
for old, new in edits:
    if old in src:
        src = src.replace(old, new, 1)
        count += 1
    else:
        # Mark patch flag once even if anchors fail to track success/fail
        pass

if count == 0:
    sys.exit(f"no anchors matched in {TARGET}")

# Sentinel for idempotency
src = src.replace(
    "// MCLE_iOS_SCC_CREATE_LOG:",
    "// MCLE_iOS_SCC_PHASES + MCLE_iOS_SCC_CREATE_LOG:",
    1,
)

TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET} ({count} phase anchors)")
