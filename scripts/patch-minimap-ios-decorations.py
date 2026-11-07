#!/usr/bin/env python3
"""Minimap::render draws the map decorations the client would have.

The client's MapItemSavedData gets its decorations from the server's
HoldingPlayer::nextUpdatePacket: the players, then the item frames and the
End portal kept in nonPlayerDecorations. The port's client reads the
server's own data, where those two lists are apart, so on iOS Minimap walks
both, in the order the packet sends them.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HDR = REPO / "upstream" / "Minecraft.World" / "MapItemSavedData.h"
SRC = REPO / "upstream" / "Minecraft.Client" / "Minimap.cpp"


def load(path):
    if not path.exists():
        sys.exit(f"missing: {path}")
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def save(path, text):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print(f"patched: {path}")


hdr = load(HDR)
if "friend class Minimap;" in hdr:
    print(f"already patched: {HDR}")
else:
    nl = "\r\n" if "\r\n" in hdr else "\n"
    anchor = "class MapItemSavedData : public SavedData" + nl + "{" + nl
    if hdr.count(anchor) != 1:
        sys.exit(f"class anchor not found once in {HDR}")
    save(HDR, hdr.replace(anchor, anchor + nl.join([
        "#ifdef __APPLE_IOS__",
        "\t// The port's client reads the server's data, see Minimap::render",
        "\tfriend class Minimap;",
        "#endif",
        "",
    ])))

src = load(SRC)
if "nonPlayerDecorations" in src:
    print(f"already patched: {SRC}")
else:
    nl = "\r\n" if "\r\n" in src else "\n"
    loop = "\tfor( MapItemSavedData::MapDecoration *dec : data->decorations )"
    anchor = loop + nl
    if src.count(anchor) != 1:
        sys.exit(f"decorations loop anchor not found once in {SRC}")
    save(SRC, src.replace(anchor, nl.join([
        "#ifdef __APPLE_IOS__",
        "\t// HoldingPlayer::nextUpdatePacket's decorations, the players then the rest",
        "\tvector<MapItemSavedData::MapDecoration *> decorations = data->decorations;",
        "\tfor (auto& it : data->nonPlayerDecorations) decorations.push_back(it.second);",
        "\tfor( MapItemSavedData::MapDecoration *dec : decorations )",
        "#else",
        loop,
        "#endif",
        "",
    ])))
