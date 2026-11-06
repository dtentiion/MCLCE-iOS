#!/usr/bin/env python3
"""G5-step29: redirect Textures::bindTexture(ResourceLocation*) to the
iOS CGImageSource PNG path that worked in G4.

Before G5-step29 we had a stub in link_stubs.cpp that loaded PNGs from
Documents/Common/res/ via mcle_glbridge_load_or_get_png_path. Removing
that stub (so upstream Textures.cpp could provide it) regressed the
sun/cloud rendering because upstream's body calls loadTexture which
needs a real TexturePack and TextureManager that aren't wired yet.

This patch replaces the upstream body with the same iOS path the
removed stub had, for the default pack. With a texture pack selected the
upstream body runs, so the pack's own textures (and the default ones it
falls back to through readImage) are what get bound. The block and item
atlases are the stitched ones for every pack, as upstream's loadTexture
gives, since that's where the animated textures draw.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "Textures.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_BINDTEXTURE" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "void Textures::bindTexture(ResourceLocation *resource)\n"
    "{\n"
    "\tif(resource->isPreloaded())\n"
    "\t{\n"
    "\t\tbind(loadTexture(resource->getTexture()));\n"
    "\t}\n"
    "\telse\n"
    "\t{\n"
    "\t\tbind(loadTexture(TN_COUNT, resource->getPath()));\n"
    "\t}\n"
    "}"
)
new = (
    "// MCLE_iOS_BINDTEXTURE: the default pack's textures go through the\n"
    "// CGImageSource-backed iOS path (was in link_stubs.cpp pre G5-step29),\n"
    "// a selected texture pack's through upstream's loadTexture.\n"
    "const char *texture_name_relpath(_TEXTURE_NAME tn);\n"
    "extern \"C\" unsigned int mcle_glbridge_load_or_get_png_path(const char *path);\n"
    "extern \"C\" void mcle_glbridge_bind_texture(unsigned int id);\n"
    "static std::string mcle_ios_wstr_to_utf8(const std::wstring &w) {\n"
    "\tstd::string out; out.reserve(w.size());\n"
    "\tfor (wchar_t c : w) {\n"
    "\t\tif (c < 0x80) out.push_back((char)c);\n"
    "\t\telse if (c < 0x800) {\n"
    "\t\t\tout.push_back((char)(0xC0 | ((c >> 6) & 0x1F)));\n"
    "\t\t\tout.push_back((char)(0x80 | (c & 0x3F)));\n"
    "\t\t} else {\n"
    "\t\t\tout.push_back((char)(0xE0 | ((c >> 12) & 0x0F)));\n"
    "\t\t\tout.push_back((char)(0x80 | ((c >> 6) & 0x3F)));\n"
    "\t\t\tout.push_back((char)(0x80 | (c & 0x3F)));\n"
    "\t\t}\n"
    "\t}\n"
    "\treturn out;\n"
    "}\n"
    "void Textures::bindTexture(ResourceLocation *resource)\n"
    "{\n"
    "\tif (!resource) return;\n"
    "\t// The block and item atlases are the stitched ones, like loadTexture's,\n"
    "\t// so what the animated textures draw into them shows\n"
    "\tif (resource->isPreloaded() && (resource->getTexture() == TN_TERRAIN || resource->getTexture() == TN_GUI_ITEMS))\n"
    "\t{\n"
    "\t\tconst bool blocks = resource->getTexture() == TN_TERRAIN;\n"
    "\t\tPreStitchedTextureMap *atlas = blocks ? terrain : items;\n"
    "\t\tTexture *stitched = atlas != nullptr ? atlas->getStitchedTexture() : nullptr;\n"
    "\t\tif (stitched != nullptr)\n"
    "\t\t{\n"
    "\t\t\tbind(loadTexture(resource->getTexture()));\n"
    "\t\t\t// The stitch runs on the game's init thread, where another\n"
    "\t\t\t// texture can be the bound one when it uploads, so the first\n"
    "\t\t\t// upload goes again here with the atlas bound\n"
    "\t\t\tstatic Texture *s_uploaded[2] = {};\n"
    "\t\t\tTexture *&uploaded = s_uploaded[blocks ? 0 : 1];\n"
    "\t\t\tif (uploaded != stitched)\n"
    "\t\t\t{\n"
    "\t\t\t\tstitched->updateOnGPU();\n"
    "\t\t\t\tuploaded = stitched;\n"
    "\t\t\t}\n"
    "\t\t\treturn;\n"
    "\t\t}\n"
    "\t}\n"
    "\t// A texture pack's textures come from the pack, through loadTexture\n"
    "\tif (!skins->isUsingDefaultSkin())\n"
    "\t{\n"
    "\t\tif(resource->isPreloaded())\n"
    "\t\t{\n"
    "\t\t\tbind(loadTexture(resource->getTexture()));\n"
    "\t\t}\n"
    "\t\telse\n"
    "\t\t{\n"
    "\t\t\tbind(loadTexture(TN_COUNT, resource->getPath()));\n"
    "\t\t}\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tstd::string rel;\n"
    "\tif (resource->isPreloaded()) {\n"
    "\t\tconst char *p = texture_name_relpath(resource->getTexture());\n"
    "\t\tif (!p) return;\n"
    "\t\trel = std::string(p) + \".png\";\n"
    "\t} else {\n"
    "\t\tstd::wstring wpath = resource->getPath();\n"
    "\t\tif (wpath.empty()) return;\n"
    "\t\trel = mcle_ios_wstr_to_utf8(wpath);\n"
    "\t}\n"
    "\tconst char *root = StorageManager.GetSaveRootPath();\n"
    "\tif (!root || !*root) return;\n"
    "\tconst std::string commonRoot = std::string(root) + \"/Common/\";\n"
    "\tconst std::string base = commonRoot + \"res/\";\n"
    "\t// Search order matches upstream's newest-TU-wins semantics.\n"
    "\t// Upstream DefaultTexturePack.cpp:100 uses\n"
    "\t// Common/res/TitleUpdate/res/ as the canonical post-tubuild drive.\n"
    "\t// On our iOS deployment the TitleUpdate folder usually lives\n"
    "\t// alongside (Common/TitleUpdate/res/) rather than inside res/,\n"
    "\t// so we try both before falling back to 1_2_2 (developer source)\n"
    "\t// and finally the TU0 base atlas.\n"
    "\tunsigned int id = mcle_glbridge_load_or_get_png_path(\n"
    "\t\t(base + \"TitleUpdate/res/\" + rel).c_str());\n"
    "\tif (id == 0) {\n"
    "\t\tid = mcle_glbridge_load_or_get_png_path((commonRoot + \"TitleUpdate/res/\" + rel).c_str());\n"
    "\t}\n"
    "\tif (id == 0) {\n"
    "\t\tid = mcle_glbridge_load_or_get_png_path((base + \"1_2_2/\" + rel).c_str());\n"
    "\t}\n"
    "\tif (id == 0) {\n"
    "\t\tid = mcle_glbridge_load_or_get_png_path((base + rel).c_str());\n"
    "\t}\n"
    "\tif (id != 0) mcle_glbridge_bind_texture(id);\n"
    "}"
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")
src = src.replace(old, new, 1)
TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
