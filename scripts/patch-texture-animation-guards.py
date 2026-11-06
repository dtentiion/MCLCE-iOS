#!/usr/bin/env python3
"""Animated textures with no frames don't cycle.

An animation's frames come from its .txt and its PNG. When the PNG doesn't
load, BufferedImage returns with its size never set, and createTextures
cuts the image into frames by that size. The compass and clock icons for
each pad share their frames with the compass and clock textures, which
only have frames when those animations loaded, and cycleFrames divides by
the frame count every tick.

- TextureManager::createTextures returns no frames for an image that
  didn't load.
- StitchedTexture, CompassTexture and ClockTexture leave the atlas as it
  is when there are no frames to pick from.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CLIENT = REPO / "upstream" / "Minecraft.Client"
MARKER = "MCLE_ANIM_GUARDS"


def patch(name, edits):
    path = CLIENT / name
    src = path.read_text(encoding="utf-8", errors="replace")
    if MARKER in src:
        print(f"already patched: {path}")
        return
    for old, new in edits:
        if src.count(old) != 1:
            sys.exit(f"anchor not found once in {path}: {old.strip().splitlines()[0]}")
        src = src.replace(old, new)
    path.write_text(src, encoding="utf-8", newline="")
    print(f"patched: {path}")


patch("TextureManager.cpp", [(
    "\tBufferedImage *image = texturePack->getImageResource(filename, false, true, drive);\n"
    "\tMemSect(0);\n"
    "\tint height = image->getHeight();\n",
    "\tBufferedImage *image = texturePack->getImageResource(filename, false, true, drive);\n"
    "\tMemSect(0);\n"
    "\t// MCLE_ANIM_GUARDS: an image that didn't load has no size to cut frames by\n"
    "\tif (image == nullptr || image->getData() == nullptr)\n"
    "\t{\n"
    "\t\tdelete image;\n"
    "\t\treturn result;\n"
    "\t}\n"
    "\tint height = image->getHeight();\n",
)])

patch("StitchedTexture.cpp", [(
    "void StitchedTexture::cycleFrames()\n"
    "{\n",
    "void StitchedTexture::cycleFrames()\n"
    "{\n"
    "\t// MCLE_ANIM_GUARDS\n"
    "\tif (frames == nullptr || frames->empty()) return;\n",
)])

FRAME_GUARD = (
    "\t// 4J Stu - We share data with another texture\n"
    "\tif(m_dataTexture != nullptr)\n"
    "\t{\n",
    "\t// 4J Stu - We share data with another texture\n"
    "\tif(m_dataTexture != nullptr)\n"
    "\t{\n"
    "\t\t// MCLE_ANIM_GUARDS\n"
    "\t\tif (m_dataTexture->frames == nullptr || m_dataTexture->frames->empty()) return;\n",
)
OWN_GUARD_OLD = "\telse\n\t{\n\t\tint newFrame = "
OWN_GUARD_NEW = "\telse\n\t{\n\t\tif (frames == nullptr || frames->empty()) return;\n\t\tint newFrame = "

patch("CompassTexture.cpp", [FRAME_GUARD, (OWN_GUARD_OLD, OWN_GUARD_NEW)])
patch("ClockTexture.cpp", [FRAME_GUARD, (OWN_GUARD_OLD, OWN_GUARD_NEW)])
