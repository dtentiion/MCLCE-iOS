#!/usr/bin/env python3
"""Free the tag of an entity that doesn't save.

OldChunkStorage::saveEntities makes a CompoundTag for every entity in the
chunk and only keeps it when Entity::save succeeds. The player, removed
entities and anything with a rider return false, so their empty tag was
lost on every incremental save (about 0.2 MB/s while playing).

Idempotent.
"""
import sys
from pathlib import Path

TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.World" / "OldChunkStorage.cpp"

src = TARGET.read_text(encoding="utf-8", errors="replace")
nl = "\r\n" if "\r\n" in src else "\n"
old = ("\t\t\tif ( e && e->save(teTag))" + nl + "\t\t\t{" + nl +
       "\t\t\t\tentityTags->add(teTag);" + nl + "\t\t\t}" + nl)
new = old + "#ifdef __APPLE_IOS__" + nl + "\t\t\telse" + nl + "\t\t\t{" + nl + "\t\t\t\tdelete teTag;" + nl + "\t\t\t}" + nl + "#endif" + nl
if new in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)
if src.count(old) != 1:
    sys.exit(f"saveEntities anchor not found in {TARGET}")
TARGET.write_text(src.replace(old, new, 1), encoding="utf-8", errors="replace", newline="")
print(f"patched {TARGET}")
