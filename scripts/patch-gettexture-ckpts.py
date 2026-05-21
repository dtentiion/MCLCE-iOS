#!/usr/bin/env python3
"""Null-guards inside TileRenderer::getTexture(Tile*) chain.

Originally bracketed each step with GTX_CKPT / GTX2_CKPT log lines
to pin an addr 0xe0 crash. The crash is fixed; the per-tile logs
were firing thousands of times per chunk rebuild and stalling the
worker threads. Keep only the structural null guards.
"""
import sys
from pathlib import Path
TR_CPP = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "TileRenderer.cpp"

src = TR_CPP.read_text(encoding="utf-8", errors="replace")
if "MCLE_GTX_GUARDS" in src:
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
        "\tif (tile == nullptr) return nullptr; // MCLE_GTX_GUARDS\n"
        "\tIcon *_gtxIcon = tile->getTexture(Facing::UP);\n"
        "\t// If tile->icon was never registered, DON'T fall into\n"
        "\t// getTextureOrMissing - it derefs minecraft->textures->getMissingIcon\n"
        "\t// which is the null deref crash. Return nullptr; the caller's\n"
        "\t// own null guard skips this tile.\n"
        "\tif (_gtxIcon == nullptr) return nullptr;\n"
        "\treturn getTextureOrMissing(_gtxIcon);\n"
        "}"
    )
    if old not in src: sys.exit("TR::getTexture anchor not found")
    src = src.replace(old, new, 1)
    TR_CPP.write_text(src, encoding="utf-8")
    print(f"patched {TR_CPP}")
