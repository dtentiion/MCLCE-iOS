#!/usr/bin/env python3
"""checkpoints in Texture::_init to localize createTexture crash"""
import sys
from pathlib import Path
TEX_CPP = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "Texture.cpp"
TM_CPP = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "TextureManager.cpp"

src = TEX_CPP.read_text(encoding="utf-8", errors="replace")
if "TEX_CKPT" in src:
    print("Texture.cpp already patched")
else:
    old = (
        "void Texture::_init(const wstring &name, int mode, int width, int height, int depth, int wrapMode, int format, int minFilter, int magFilter, bool mipMap)\n"
        "{\n"
    )
    new = (
        "void Texture::_init(const wstring &name, int mode, int width, int height, int depth, int wrapMode, int format, int minFilter, int magFilter, bool mipMap)\n"
        "{\n"
        "\tapp.DebugPrintf(\"TEX_CKPT _init noimg entry w=%d h=%d d=%d mode=%d\", width, height, depth, mode);\n"
    )
    if old not in src:
        sys.exit("Texture _init noimg anchor not found")
    src = src.replace(old, new, 1)

    old2 = (
        "\tif (mode != TM_CONTAINER)\n"
        "\t{\n"
        "\t\tglId = glGenTextures();\n"
        "\n"
        "\t\tglBindTexture(type, glId);\n"
        "\t\tglTexParameteri(type, GL_TEXTURE_MIN_FILTER, minFilter);\n"
        "\t\tglTexParameteri(type, GL_TEXTURE_MAG_FILTER, magFilter);\n"
        "\t\tglTexParameteri(type, GL_TEXTURE_WRAP_S, wrapMode);\n"
        "\t\tglTexParameteri(type, GL_TEXTURE_WRAP_T, wrapMode);\n"
        "\t}\n"
        "\telse\n"
        "\t{\n"
        "\t\tglId = -1;\n"
        "\t}\n"
        "\n"
        "\tmanagerId = TextureManager::getInstance()->createTextureID();"
    )
    new2 = (
        "\tapp.DebugPrintf(\"TEX_CKPT _init before glGenTextures mode=%d\", mode);\n"
        "\tif (mode != TM_CONTAINER)\n"
        "\t{\n"
        "\t\tglId = glGenTextures();\n"
        "\t\tapp.DebugPrintf(\"TEX_CKPT _init after glGenTextures glId=%d\", glId);\n"
        "\t\tglBindTexture(type, glId);\n"
        "\t\tapp.DebugPrintf(\"TEX_CKPT _init after glBindTexture\");\n"
        "\t\tglTexParameteri(type, GL_TEXTURE_MIN_FILTER, minFilter);\n"
        "\t\tglTexParameteri(type, GL_TEXTURE_MAG_FILTER, magFilter);\n"
        "\t\tglTexParameteri(type, GL_TEXTURE_WRAP_S, wrapMode);\n"
        "\t\tglTexParameteri(type, GL_TEXTURE_WRAP_T, wrapMode);\n"
        "\t\tapp.DebugPrintf(\"TEX_CKPT _init after glTexParameteri x4\");\n"
        "\t}\n"
        "\telse\n"
        "\t{\n"
        "\t\tglId = -1;\n"
        "\t}\n"
        "\n"
        "\tapp.DebugPrintf(\"TEX_CKPT _init before createTextureID\");\n"
        "\tmanagerId = TextureManager::getInstance()->createTextureID();\n"
        "\tapp.DebugPrintf(\"TEX_CKPT _init after createTextureID managerId=%d\", managerId);"
    )
    if old2 not in src:
        sys.exit("Texture _init glGen anchor not found")
    # The replace_all-equivalent: use replace, anchor is unique
    # but we need to drop the original glId = glGenTextures(); line that follows
    # Actually the new2 contains glGenTextures() too, so replace once
    src = src.replace(old2, new2, 1)

    old3 = (
        "void Texture::_init(const wstring &name, int mode, int width, int height, int depth, int wrapMode, int format, int minFilter, int magFilter, BufferedImage *image, bool mipMap)\n"
        "{\n"
        "\t_init(name, mode, width, height, depth, wrapMode, format, minFilter, magFilter, mipMap);\n"
    )
    new3 = (
        "void Texture::_init(const wstring &name, int mode, int width, int height, int depth, int wrapMode, int format, int minFilter, int magFilter, BufferedImage *image, bool mipMap)\n"
        "{\n"
        "\tapp.DebugPrintf(\"TEX_CKPT _init withimg entry image=%p w=%d h=%d\", image, width, height);\n"
        "\t_init(name, mode, width, height, depth, wrapMode, format, minFilter, magFilter, mipMap);\n"
        "\tapp.DebugPrintf(\"TEX_CKPT _init withimg after noimg-init\");\n"
    )
    if old3 not in src:
        sys.exit("Texture _init withimg anchor not found")
    src = src.replace(old3, new3, 1)

    TEX_CPP.write_text(src, encoding="utf-8")
    print(f"patched {TEX_CPP}")

src = TM_CPP.read_text(encoding="utf-8", errors="replace")
if "TM_CKPT" in src:
    print("TextureManager.cpp already patched")
else:
    old = (
        "Texture *TextureManager::createTexture(const wstring &name, int mode, int width, int height, int wrap, int format, int minFilter, int magFilter, bool mipmap, BufferedImage *image)\n"
        "{\n"
        "\tTexture *newTex = new Texture(name, mode, width, height, wrap, format, minFilter, magFilter, image, mipmap);\n"
        "\tregisterTexture(newTex);\n"
        "\treturn newTex;\n"
        "}"
    )
    new = (
        "Texture *TextureManager::createTexture(const wstring &name, int mode, int width, int height, int wrap, int format, int minFilter, int magFilter, bool mipmap, BufferedImage *image)\n"
        "{\n"
        "\tapp.DebugPrintf(\"TM_CKPT createTexture entry w=%d h=%d image=%p\", width, height, image);\n"
        "\tTexture *newTex = new Texture(name, mode, width, height, wrap, format, minFilter, magFilter, image, mipmap);\n"
        "\tapp.DebugPrintf(\"TM_CKPT createTexture after new Texture %p\", newTex);\n"
        "\tregisterTexture(newTex);\n"
        "\tapp.DebugPrintf(\"TM_CKPT createTexture after registerTexture\");\n"
        "\treturn newTex;\n"
        "}"
    )
    if old not in src:
        sys.exit("TM createTexture anchor not found")
    src = src.replace(old, new, 1)

    TM_CPP.write_text(src, encoding="utf-8")
    print(f"patched {TM_CPP}")
