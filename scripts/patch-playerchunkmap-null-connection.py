#!/usr/bin/env python3
"""Guard PlayerChunkMap connection->send calls against null connection.

Our local single-player has no real network connection on the
ServerPlayer; mc->player->connection is null. Upstream PlayerChunkMap
sends ChunkVisibility packets via connection->send to notify clients
which chunks are loading. With no connection, every send call null-
derefs.

Add a `if (player->connection)` guard around each send site so the
streaming logic runs but skips the packet broadcast.

Idempotent.
"""
import sys, re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "upstream" / "Minecraft.Client" / "PlayerChunkMap.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_PCM_NULL_CONN" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    # PlayerChunkMap::add at line ~663
    (
        "\tplayer->connection->send(std::make_shared<ChunkVisibilityAreaPacket>(minX, maxX, minZ, maxZ));",
        "\tif (player->connection) player->connection->send(std::make_shared<ChunkVisibilityAreaPacket>(minX, maxX, minZ, maxZ)); /* MCLE_iOS_PCM_NULL_CONN */",
    ),
    # PlayerChunk::add (sendPacket-guarded already, but inside it deref's connection)
    (
        "\tif( sendPacket ) player->connection->send(std::make_shared<ChunkVisibilityPacket>(pos.x, pos.z, true));",
        "\tif( sendPacket && player->connection ) player->connection->send(std::make_shared<ChunkVisibilityPacket>(pos.x, pos.z, true)); /* MCLE_iOS_PCM_NULL_CONN */",
    ),
    # PlayerChunk::remove
    (
        "\t\t\t\tplayer->connection->send(std::make_shared<ChunkVisibilityPacket>(pos.x, pos.z, false));",
        "\t\t\t\tif (player->connection) player->connection->send(std::make_shared<ChunkVisibilityPacket>(pos.x, pos.z, false)); /* MCLE_iOS_PCM_NULL_CONN */",
    ),
]

count = 0
for old, new in edits:
    if old in src:
        src = src.replace(old, new, 1)
        count += 1

if count == 0:
    sys.exit(f"no anchors matched in {TARGET}")

TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET} ({count} sites)")
