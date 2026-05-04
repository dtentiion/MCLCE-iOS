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
    "\tBufferedImage *image = texturePack->getImageResource(filename, false, true, drive);\n"
    "\tMemSect(0);\n"
    "\tint height = image->getHeight();\n"
    "\tint width = image->getWidth();\n"
    "\n"
    "\tif(stitchResult != nullptr)\n"
    "\t{\n"
    "\t\tTextureManager::getInstance()->unregisterTexture(name, stitchResult);\n"
    "\t\tdelete stitchResult;\n"
    "\t}\n"
    "\tstitchResult = TextureManager::getInstance()->createTexture(name, Texture::TM_DYNAMIC, width, height, Texture::TFMT_RGBA, m_mipMap);\n"
    "\tstitchResult->transferFromImage(image);\n"
    "\tdelete image;\n"
    "\tTextureManager::getInstance()->registerName(name, stitchResult);"
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
    '\tapp.DebugPrintf("STITCH_CKPT after getImageResource image=%p", image);\n'
    "\tMemSect(0);\n"
    "\tif (!image) {\n"
    '\t\tapp.DebugPrintf("STITCH_CKPT bail: image null");\n'
    "\t\treturn;\n"
    "\t}\n"
    '\tapp.DebugPrintf("STITCH_CKPT before image->getHeight");\n'
    "\tint height = image->getHeight();\n"
    '\tapp.DebugPrintf("STITCH_CKPT before image->getWidth");\n'
    "\tint width = image->getWidth();\n"
    '\tapp.DebugPrintf("STITCH_CKPT image dims wxh = %dx%d", width, height);\n'
    "\n"
    "\tif(stitchResult != nullptr)\n"
    "\t{\n"
    '\t\tapp.DebugPrintf("STITCH_CKPT before unregisterTexture");\n'
    "\t\tTextureManager::getInstance()->unregisterTexture(name, stitchResult);\n"
    "\t\tdelete stitchResult;\n"
    "\t}\n"
    '\tapp.DebugPrintf("STITCH_CKPT before createTexture");\n'
    "\tstitchResult = TextureManager::getInstance()->createTexture(name, Texture::TM_DYNAMIC, width, height, Texture::TFMT_RGBA, m_mipMap);\n"
    '\tapp.DebugPrintf("STITCH_CKPT after createTexture stitchResult=%p", stitchResult);\n'
    '\tapp.DebugPrintf("STITCH_CKPT before transferFromImage");\n'
    "\tstitchResult->transferFromImage(image);\n"
    '\tapp.DebugPrintf("STITCH_CKPT before delete image");\n'
    "\tdelete image;\n"
    '\tapp.DebugPrintf("STITCH_CKPT before registerName");\n'
    "\tTextureManager::getInstance()->registerName(name, stitchResult);\n"
    '\tapp.DebugPrintf("STITCH_CKPT after registerName");'
)
src = src.replace(old5, new5, 1)

# CKPTs around the texturesByName loops + writeAsPNG + updateOnGPU
old6 = (
    "\tfor(auto & it : texturesByName)\n"
    "\t{\n"
    "\t\tauto *preStitched = static_cast<StitchedTexture *>(it.second);\n"
    "\n"
    "\t\tint x = preStitched->getU0() * stitchResult->getWidth();\n"
    "\t\tint y = preStitched->getV0() * stitchResult->getHeight();\n"
    "\t\tint width = (preStitched->getU1() * stitchResult->getWidth()) - x;\n"
    "\t\tint height = (preStitched->getV1() * stitchResult->getHeight()) - y;\n"
    "\n"
    "\t\tpreStitched->init(stitchResult, nullptr, x, y, width, height, false);\n"
    "\t}"
)
new6 = (
    '\tapp.DebugPrintf("STITCH_CKPT before texturesByName init loop, count=%zu", texturesByName.size());\n'
    "\tfor(auto & it : texturesByName)\n"
    "\t{\n"
    "\t\tauto *preStitched = static_cast<StitchedTexture *>(it.second);\n"
    "\n"
    "\t\tint x = preStitched->getU0() * stitchResult->getWidth();\n"
    "\t\tint y = preStitched->getV0() * stitchResult->getHeight();\n"
    "\t\tint width = (preStitched->getU1() * stitchResult->getWidth()) - x;\n"
    "\t\tint height = (preStitched->getV1() * stitchResult->getHeight()) - y;\n"
    "\n"
    "\t\tpreStitched->init(stitchResult, nullptr, x, y, width, height, false);\n"
    "\t}\n"
    '\tapp.DebugPrintf("STITCH_CKPT after texturesByName init loop");'
)
src = src.replace(old6, new6, 1)

old7 = (
    "\tstitchResult->writeAsPNG(L\"debug.stitched_\" + name + L\".png\");\n"
    "\tstitchResult->updateOnGPU();"
)
new7 = (
    '\tapp.DebugPrintf("STITCH_CKPT before writeAsPNG");\n'
    "\tstitchResult->writeAsPNG(L\"debug.stitched_\" + name + L\".png\");\n"
    '\tapp.DebugPrintf("STITCH_CKPT before updateOnGPU");\n'
    "\tstitchResult->updateOnGPU();\n"
    '\tapp.DebugPrintf("STITCH_CKPT done");'
)
src = src.replace(old7, new7, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
