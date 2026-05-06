#!/usr/bin/env python3
"""bracket TileRenderer::getTexture(Tile*) and Tile::getTexture chain to find addr 0xe0 crash"""
import sys
from pathlib import Path
TR_CPP = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "TileRenderer.cpp"
T_CPP = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.World" / "Tile.cpp"

src = TR_CPP.read_text(encoding="utf-8", errors="replace")
if "GTX_CKPT" in src:
    print(f"already patched: {TR_CPP}")
else:
    old = (
        "Icon *TileRenderer::getTexture(Tile *tile)\n"
        "{\n"
        "\treturn getTextureOrMissing(tile->getTexture(Facing::UP));\n"
        "}"
    )
    new = (
        "Icon *TileRenderer::getTexture(Tile *tile)\n"
        "{\n"
        "\tapp.DebugPrintf(\"GTX_CKPT TR::getTexture entry tile=%p\", tile);\n"
        "\tif (tile == nullptr) { app.DebugPrintf(\"GTX_CKPT bail: tile null\"); return nullptr; }\n"
        "\tapp.DebugPrintf(\"GTX_CKPT before tile->getTexture(UP)\");\n"
        "\tIcon *_gtxIcon = tile->getTexture(Facing::UP);\n"
        "\tapp.DebugPrintf(\"GTX_CKPT after tile->getTexture(UP) icon=%p\", _gtxIcon);\n"
        "\treturn getTextureOrMissing(_gtxIcon);\n"
        "}"
    )
    if old not in src: sys.exit("TR::getTexture anchor not found")
    src = src.replace(old, new, 1)
    TR_CPP.write_text(src, encoding="utf-8")
    print(f"patched {TR_CPP}")

src = T_CPP.read_text(encoding="utf-8", errors="replace")
if "GTX2_CKPT" in src:
    print(f"already patched: {T_CPP}")
else:
    # Tile::getTexture(int face)
    old = (
        "Icon *Tile::getTexture(int face)\n"
        "{\n"
        "\treturn getTexture(face, 0);\n"
        "}"
    )
    new = (
        "Icon *Tile::getTexture(int face)\n"
        "{\n"
        "\tapp.DebugPrintf(\"GTX2_CKPT Tile::getTexture(face=%d) this=%p\", face, this);\n"
        "\tif (this == nullptr) { app.DebugPrintf(\"GTX2_CKPT this null\"); return nullptr; }\n"
        "\treturn getTexture(face, 0);\n"
        "}"
    )
    if old not in src: sys.exit("Tile::getTexture(int) anchor not found")
    src = src.replace(old, new, 1)

    # Tile::getTexture(int face, int data)
    old2 = (
        "Icon *Tile::getTexture(int face, int data)\n"
        "{\n"
        "\treturn icon;\n"
        "}"
    )
    new2 = (
        "Icon *Tile::getTexture(int face, int data)\n"
        "{\n"
        "\tapp.DebugPrintf(\"GTX2_CKPT Tile::getTexture(face=%d,data=%d) this=%p icon=%p\", face, data, this, icon);\n"
        "\treturn icon;\n"
        "}"
    )
    if old2 not in src: sys.exit("Tile::getTexture(int,int) anchor not found")
    src = src.replace(old2, new2, 1)

    T_CPP.write_text(src, encoding="utf-8")
    print(f"patched {T_CPP}")
