#!/usr/bin/env python3
"""Bracket Region + TileRenderer ctor/dtor with mcle_alloc_inc/dec so
MEMSTATS can report live + total counts. If a Region or TileRenderer
counter live=N goes up forever while total keeps climbing, that class
is leaking its instances during chunk rebuild. Idempotent.
"""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent / "upstream"

REGION = ROOT / "Minecraft.World" / "Region.cpp"
TR     = ROOT / "Minecraft.Client" / "TileRenderer.cpp"
LC     = ROOT / "Minecraft.World" / "LevelChunk.cpp"

EXTERN_DECLS = (
    "#ifdef __APPLE_IOS__\n"
    "extern \"C\" void mcle_alloc_inc(int tag);\n"
    "extern \"C\" void mcle_alloc_dec(int tag);\n"
    "#endif\n"
)

# --- Region ---
src = REGION.read_text(encoding="utf-8", errors="replace")
if "MCLE_LEAK_REGION" in src:
    print(f"already patched: {REGION}")
else:
    # Insert decls at the top after stdafx include.
    if '#include "stdafx.h"\n' in src:
        src = src.replace('#include "stdafx.h"\n', '#include "stdafx.h"\n' + EXTERN_DECLS, 1)

    # Patch ctor body opening brace.
    old = (
        "Region::Region(Level *level, int x1, int y1, int z1, int x2, int y2, int z2, int r)\n"
        "{\n"
    )
    new = (
        "Region::Region(Level *level, int x1, int y1, int z1, int x2, int y2, int z2, int r)\n"
        "{\n"
        "#ifdef __APPLE_IOS__\n"
        "\tmcle_alloc_inc(0); // MCLE_LEAK_REGION\n"
        "#endif\n"
    )
    if old not in src: sys.exit(f"Region ctor anchor not found in {REGION}")
    src = src.replace(old, new, 1)

    # Patch dtor body opening brace.
    old2 = "Region::~Region()\n{\n"
    new2 = (
        "Region::~Region()\n{\n"
        "#ifdef __APPLE_IOS__\n"
        "\tmcle_alloc_dec(0);\n"
        "#endif\n"
    )
    if old2 not in src: sys.exit(f"Region dtor anchor not found in {REGION}")
    src = src.replace(old2, new2, 1)

    REGION.write_text(src, encoding="utf-8")
    print(f"patched: {REGION}")

# --- TileRenderer ---
src = TR.read_text(encoding="utf-8", errors="replace")
if "MCLE_LEAK_TR" in src:
    print(f"already patched: {TR}")
else:
    if '#include "stdafx.h"\n' in src:
        src = src.replace('#include "stdafx.h"\n', '#include "stdafx.h"\n' + EXTERN_DECLS, 1)

    # Patch all 3 TileRenderer ctor variants so the dec from ~TileRenderer
    # always has a matching inc - else live count drifts negative for any
    # other-ctor TileRenderer that gets destructed.
    ctor_sigs = [
        "TileRenderer::TileRenderer( LevelSource* level, int xMin, int yMin, int zMin, unsigned char *tileIds )\n",
        "TileRenderer::TileRenderer( LevelSource* level )\n",
        "TileRenderer::TileRenderer()\n",
    ]
    insert = "{\n#ifdef __APPLE_IOS__\n\tmcle_alloc_inc(1); // MCLE_LEAK_TR\n#endif\n"
    found_any = False
    for sig in ctor_sigs:
        if sig not in src: continue
        found_any = True
        idx = src.find(sig)
        open_brace = src.find("{\n", idx)
        if open_brace == -1: continue
        src = src[:open_brace] + insert + src[open_brace + 2:]
    if not found_any: sys.exit(f"no TileRenderer ctor signatures matched in {TR}")

    # Dtor.
    old2 = "TileRenderer::~TileRenderer()\n{\n"
    new2 = (
        "TileRenderer::~TileRenderer()\n{\n"
        "#ifdef __APPLE_IOS__\n"
        "\tmcle_alloc_dec(1);\n"
        "#endif\n"
    )
    if old2 not in src: sys.exit(f"TileRenderer dtor anchor not found in {TR}")
    src = src.replace(old2, new2, 1)

    TR.write_text(src, encoding="utf-8")
    print(f"patched: {TR}")

# --- LevelChunk ---
src = LC.read_text(encoding="utf-8", errors="replace")
if "MCLE_LEAK_LC" in src:
    print(f"already patched: {LC}")
else:
    if '#include "stdafx.h"\n' in src:
        src = src.replace('#include "stdafx.h"\n', '#include "stdafx.h"\n' + EXTERN_DECLS, 1)

    # Three LevelChunk ctor variants. All increment counter 2.
    ctor_sigs = [
        "LevelChunk::LevelChunk(Level *level, int x, int z)\n",
        "LevelChunk::LevelChunk(Level *level, byteArray blocks, int x, int z)\n",
        "LevelChunk::LevelChunk(Level *level, int x, int z, LevelChunk *lc)\n",
    ]
    insert = "{\n#ifdef __APPLE_IOS__\n\tmcle_alloc_inc(2); // MCLE_LEAK_LC\n#endif\n"
    found_any = False
    for sig in ctor_sigs:
        if sig not in src: continue
        found_any = True
        idx = src.find(sig)
        open_brace = src.find("{\n", idx)
        if open_brace == -1: continue
        src = src[:open_brace] + insert + src[open_brace + 2:]
    if not found_any: sys.exit(f"no LevelChunk ctor signatures matched in {LC}")

    # Dtor.
    old = "LevelChunk::~LevelChunk()\n{\n"
    new = (
        "LevelChunk::~LevelChunk()\n{\n"
        "#ifdef __APPLE_IOS__\n"
        "\tmcle_alloc_dec(2);\n"
        "#endif\n"
    )
    if old not in src: sys.exit(f"LevelChunk dtor anchor not found in {LC}")
    src = src.replace(old, new, 1)

    LC.write_text(src, encoding="utf-8")
    print(f"patched: {LC}")
