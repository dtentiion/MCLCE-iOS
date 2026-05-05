#!/usr/bin/env python3
"""log entry to BufferedImage(wstring) ctor"""
import sys
from pathlib import Path
TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "BufferedImage.cpp"
src = TARGET.read_text(encoding="utf-8", errors="replace")
if "BIM_CKPT" in src:
    print("already patched"); sys.exit(0)

old = (
    "BufferedImage::BufferedImage(const wstring& File, bool filenameHasExtension /*=false*/, bool bTitleUpdateTexture /*=false*/, const wstring &drive /*=L\"\"*/)\n"
    "{\n"
    "\tHRESULT hr;"
)
new = (
    "BufferedImage::BufferedImage(const wstring& File, bool filenameHasExtension /*=false*/, bool bTitleUpdateTexture /*=false*/, const wstring &drive /*=L\"\"*/)\n"
    "{\n"
    "\tapp.DebugPrintf(\"BIM_CKPT ctor wstring entry, File.size=%zu\", File.size());\n"
    "\tHRESULT hr;"
)
if old not in src:
    sys.exit("anchor not found")
src = src.replace(old, new, 1)

# Also bracket the LoadTextureData call
old2 = (
    "\t\thr=RenderManager.LoadTextureData(pchTextureName,&ImageInfo,&data[l]);"
)
new2 = (
    "\t\tapp.DebugPrintf(\"BIM_CKPT before LoadTextureData l=%d name=%s\", l, pchTextureName ? pchTextureName : \"(null)\");\n"
    "\t\thr=RenderManager.LoadTextureData(pchTextureName,&ImageInfo,&data[l]);\n"
    "\t\tapp.DebugPrintf(\"BIM_CKPT after LoadTextureData hr=%ld\", (long)hr);"
)
if old2 not in src:
    sys.exit("anchor 2 not found")
src = src.replace(old2, new2, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched {TARGET}")
