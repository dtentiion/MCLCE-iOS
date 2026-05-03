#!/usr/bin/env python3
"""G5-step5: bracket LevelRenderer::updateDirtyChunks() with diagnostic
log lines so we can pin which check is bailing - the chunk grid is
allocated but no chunks rebuild even with the spawn chunk loaded.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "LevelRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "UDC_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # Function entry: log mc / dirtyChunkPresent on entry.
    (
        "bool LevelRenderer::updateDirtyChunks()\n{\n",
        "bool LevelRenderer::updateDirtyChunks()\n{\n"
        '\tstatic int s_udcLogCount = 0;\n'
        '\tbool s_log = (s_udcLogCount++ < 3);\n'
        '\tif (s_log) app.DebugPrintf("UDC_CKPT enter mc=%p dirtyChunkPresent=%d", mc, (int)dirtyChunkPresent);\n',
    ),
    # After queue drain: log dirtyChunkPresent.
    (
        "\t// Only bother searching round all the chunks if we have some dirty chunk(s)\n"
        "\tif( dirtyChunkPresent )\n"
        "\t{\n",
        "\tif (s_log) app.DebugPrintf(\"UDC_CKPT after queue drain dirtyChunkPresent=%d\", (int)dirtyChunkPresent);\n"
        "\t// Only bother searching round all the chunks if we have some dirty chunk(s)\n"
        "\tif( dirtyChunkPresent )\n"
        "\t{\n",
    ),
    # Inside the for(p) loop, log each early bail and the player check.
    (
        "\t\tfor( int p = 0; p < XUSER_MAX_COUNT; p++ )\n"
        "\t\t{\n"
        "\t\t\t// It's possible that the localplayers member can be set to nullptr on the main thread when a player chooses to exit the game\n"
        "\t\t\t// So take a reference to the player object now. As it is a shared_ptr it should live as long as we need it\n"
        "\t\t\tshared_ptr<LocalPlayer> player = mc->localplayers[p];\n"
        "\t\t\tif( player == nullptr ) continue;\n"
        "\t\t\tif( chunks[p].data == nullptr ) continue;\n"
        "\t\t\tif( level[p] == nullptr ) continue;\n"
        "\t\t\tif( chunks[p].length != xChunks * zChunks * CHUNK_Y_COUNT ) continue;",
        "\t\tfor( int p = 0; p < XUSER_MAX_COUNT; p++ )\n"
        "\t\t{\n"
        "\t\t\tshared_ptr<LocalPlayer> player = mc->localplayers[p];\n"
        "\t\t\tif (s_log) app.DebugPrintf(\"UDC_CKPT p=%d player=%p chunks.data=%p chunks.length=%u level=%p xChunks*zChunks*Yc=%d\","
        " p, (void*)player.get(), (void*)chunks[p].data, (unsigned)chunks[p].length, (void*)level[p],"
        " xChunks * zChunks * CHUNK_Y_COUNT);\n"
        "\t\t\tif( player == nullptr ) { if (s_log) app.DebugPrintf(\"UDC_CKPT p=%d skip: player null\", p); continue; }\n"
        "\t\t\tif( chunks[p].data == nullptr ) { if (s_log) app.DebugPrintf(\"UDC_CKPT p=%d skip: chunks.data null\", p); continue; }\n"
        "\t\t\tif( level[p] == nullptr ) { if (s_log) app.DebugPrintf(\"UDC_CKPT p=%d skip: level null\", p); continue; }\n"
        "\t\t\tif( chunks[p].length != xChunks * zChunks * CHUNK_Y_COUNT ) { if (s_log) app.DebugPrintf(\"UDC_CKPT p=%d skip: length mismatch\", p); continue; }",
    ),
    # Log dirty count and considered count at end of player loop.
    (
        "\t\t\tint considered = 0;\n"
        "\t\t\tint wouldBeNearButEmpty = 0;",
        "\t\t\tint considered = 0;\n"
        "\t\t\tint wouldBeNearButEmpty = 0;\n"
        "\t\t\tint dirtyCount = 0;\n"
        "\t\t\tint emptyCount = 0;",
    ),
    # Count dirty chunks
    (
        "\t\t\t\t\t\tif( globalChunkFlags[ pClipChunk->globalIdx ] & CHUNK_FLAG_DIRTY )\n"
        "\t\t\t\t\t\t{",
        "\t\t\t\t\t\tif( globalChunkFlags[ pClipChunk->globalIdx ] & CHUNK_FLAG_DIRTY )\n"
        "\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\tdirtyCount++;",
    ),
    # Log dirtyCount inside the chunk loop (after dirtyCount++) so we see
    # at least how many dirty chunks the loop saw on the first call.
    (
        "\t\t\t\t\t\t\tdirtyCount++;",
        "\t\t\t\t\t\t\tdirtyCount++;\n"
        "\t\t\t\t\t\t\tif (s_log && dirtyCount == 1) app.DebugPrintf(\"UDC_CKPT first dirty chunk found at idx=%d\", pClipChunk->globalIdx);",
    ),
]

patched = src
warnings = 0
for anchor, replacement in edits:
    if anchor in patched:
        patched = patched.replace(anchor, replacement, 1)
    else:
        print(f"warning: anchor not found: {anchor[:80]!r}")
        warnings += 1

if warnings:
    print(f"{warnings} anchors missed; writing partial patch")

TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
