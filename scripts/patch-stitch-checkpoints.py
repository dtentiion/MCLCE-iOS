#!/usr/bin/env python3
"""minimal stitch CKPTs - just the major step boundaries"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "PreStitchedTextureMap.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "STITCH_CKPT" in src:
    print("already patched"); sys.exit(0)

old = (
    "void PreStitchedTextureMap::stitch()\n"
    "{\n"
    "\t// Animated StitchedTextures store a vector of textures for each frame of the animation. Free any pre-existing ones here.\n"
    "\tfor(StitchedTexture *animatedStitchedTexture : animatedTextures)"
)
new = (
    "void PreStitchedTextureMap::stitch()\n"
    "{\n"
    '\tapp.DebugPrintf("STITCH_CKPT enter type=%d", iconType);\n'
    "\t// Animated StitchedTextures store a vector of textures for each frame of the animation. Free any pre-existing ones here.\n"
    "\tfor(StitchedTexture *animatedStitchedTexture : animatedTextures)"
)
src = src.replace(old, new, 1)

old2 = "\tloadUVs();\n"
new2 = (
    '\tapp.DebugPrintf("STITCH_CKPT before loadUVs");\n'
    "\tloadUVs();\n"
    '\tapp.DebugPrintf("STITCH_CKPT after loadUVs");\n'
)
src = src.replace(old2, new2, 1)

old3 = (
    "\t\tMinecraft::GetInstance()->levelRenderer->registerTextures(this);\n"
    "\t\tEntityRenderDispatcher::instance->registerTerrainTextures(this);\n"
    "\t}"
)
new3 = (
    '\t\tapp.DebugPrintf("STITCH_CKPT before levelRenderer->registerTextures");\n'
    "\t\tMinecraft::GetInstance()->levelRenderer->registerTextures(this);\n"
    '\t\tapp.DebugPrintf("STITCH_CKPT before EntityRenderDispatcher::registerTerrainTextures");\n'
    "\t\tif (EntityRenderDispatcher::instance) EntityRenderDispatcher::instance->registerTerrainTextures(this);\n"
    '\t\tapp.DebugPrintf("STITCH_CKPT after EntityRenderDispatcher");\n'
    "\t}"
)
src = src.replace(old3, new3, 1)

old4 = "\tStitcher *stitcher = TextureManager::getInstance()->createStitcher(name);"
new4 = (
    '\tapp.DebugPrintf("STITCH_CKPT before TextureManager::createStitcher");\n'
    "\tStitcher *stitcher = TextureManager::getInstance()->createStitcher(name);\n"
    '\tapp.DebugPrintf("STITCH_CKPT after createStitcher stitcher=%p", stitcher);\n'
)
src = src.replace(old4, new4, 1)

old5 = (
    "\tif(texturePack->hasFile(L\"res/\" + filename,false))\n"
    "\t{\n"
    "\t\tdrive = texturePack->getPath(true);\n"
    "\t}\n"
    "\telse\n"
    "\t{\n"
    "\t\tdrive = Minecraft::GetInstance()->skins->getDefault()->getPath(true);\n"
    "\t\ttexturePack = Minecraft::GetInstance()->skins->getDefault();\n"
    "\t}\n"
    "\n"
    "\t//BufferedImage *image = new BufferedImage(texturePack->getResource(L\"/\" + filename),false,true,drive); //ImageIO::read(texturePack->getResource(L\"/\" + filename));\n"
    "\tBufferedImage *image = texturePack->getImageResource(filename, false, true, drive);"
)
new5 = (
    '\tapp.DebugPrintf("STITCH_CKPT texturePack=%p", texturePack);\n'
    '\tapp.DebugPrintf("STITCH_CKPT before hasFile");\n'
    "\tif(texturePack->hasFile(L\"res/\" + filename,false))\n"
    "\t{\n"
    '\t\tapp.DebugPrintf("STITCH_CKPT hasFile=true, before getPath");\n'
    "\t\tdrive = texturePack->getPath(true);\n"
    '\t\tapp.DebugPrintf("STITCH_CKPT after getPath");\n'
    "\t}\n"
    "\telse\n"
    "\t{\n"
    '\t\tapp.DebugPrintf("STITCH_CKPT hasFile=false, fallback path");\n'
    "\t\tdrive = Minecraft::GetInstance()->skins->getDefault()->getPath(true);\n"
    "\t\ttexturePack = Minecraft::GetInstance()->skins->getDefault();\n"
    "\t}\n"
    "\n"
    '\tapp.DebugPrintf("STITCH_CKPT before getImageResource texturePack=%p", texturePack);\n'
    "\tBufferedImage *image = texturePack->getImageResource(filename, false, true, drive);\n"
    '\tapp.DebugPrintf("STITCH_CKPT after getImageResource image=%p", image);'
)
src = src.replace(old5, new5, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
