#!/usr/bin/env python3
"""Let the port set TexturePackRepository's selected pack.

The port builds the repository without running its constructor, so
`selected` stays null. getSelected is stubbed to the default pack, but
isUsingDefaultSkin compares the field itself, and ServerLevel::saveToDisc
then treats the pack as a DLC one and calls into it as a DLCTexturePack.
iOS gets a setter so init can select the default pack.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HDR = REPO / "upstream" / "Minecraft.Client" / "TexturePackRepository.h"

if not HDR.exists():
    sys.exit(f"missing: {HDR}")
src = HDR.read_text(encoding="utf-8", errors="replace")
if "iosSetSelected" in src:
    print(f"already patched: {HDR}")
    sys.exit(0)

anchor = "\tbool isUsingDefaultSkin() { return selected == DEFAULT_TEXTURE_PACK; } // 4J Added"
if src.count(anchor) != 1:
    sys.exit(f"anchor not found once in {HDR}")
src = src.replace(anchor, anchor + """
#ifdef __APPLE_IOS__
	// The port makes the repository without its constructor, this picks its pack
	void iosSetSelected(TexturePack *pack) { selected = pack; }
#endif""")
HDR.write_text(src, encoding="utf-8")
print(f"patched: {HDR}")
