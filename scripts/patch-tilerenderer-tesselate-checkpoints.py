#!/usr/bin/env python3
"""G5-step22: bracket the entry of TileRenderer::tesselateInWorld with
TR_CKPT lines so we pin which deref crashes for tileId=1 (stone).

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "TileRenderer.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "TR_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

old = (
    "bool TileRenderer::tesselateInWorld( Tile* tt, int x, int y, int z, int forceData,\n"
    "\t\t\t\t\t\t\t\t\tshared_ptr< TileEntity > forceEntity )\t// 4J added forceData, forceEntity param\n"
    "{\n"
    "\tTesselator* t = Tesselator::getInstance();\n"
    "\tint\tshape = tt->getRenderShape();\n"
    "\ttt->updateShape( level, x, y, z, forceData, forceEntity );\n"
    "\t// AP - now that the culling is done earlier we don't need to call setShape until later on (only for SHAPE_BLOCK)\n"
    "\tif( shape != Tile::SHAPE_BLOCK )\n"
    "\t{\n"
    "\t\tsetShape(tt);\n"
    "\t}\n"
    "\tt->setMipmapEnable( Tile::mipmapEnable[tt->id] );\t// 4J added"
)
new = (
    "bool TileRenderer::tesselateInWorld( Tile* tt, int x, int y, int z, int forceData,\n"
    "\t\t\t\t\t\t\t\t\tshared_ptr< TileEntity > forceEntity )\t// 4J added forceData, forceEntity param\n"
    "{\n"
    '\tstatic int s_trCount = 0;\n'
    '\tbool s_trLog = (s_trCount++ < 5);\n'
    '\tif (s_trLog) app.DebugPrintf("TR_CKPT enter tt=%p id=%d xyz=%d,%d,%d level=%p", tt, (int)tt->id, x, y, z, level);\n'
    "\tTesselator* t = Tesselator::getInstance();\n"
    '\tif (s_trLog) app.DebugPrintf("TR_CKPT t=%p", t);\n'
    "\tint\tshape = tt->getRenderShape();\n"
    '\tif (s_trLog) app.DebugPrintf("TR_CKPT shape=%d", shape);\n'
    '\tif (s_trLog) app.DebugPrintf("TR_CKPT before updateShape");\n'
    "\ttt->updateShape( level, x, y, z, forceData, forceEntity );\n"
    '\tif (s_trLog) app.DebugPrintf("TR_CKPT after updateShape");\n'
    "\tif( shape != Tile::SHAPE_BLOCK )\n"
    "\t{\n"
    '\t\tif (s_trLog) app.DebugPrintf("TR_CKPT before setShape (non-BLOCK)");\n'
    "\t\tsetShape(tt);\n"
    '\t\tif (s_trLog) app.DebugPrintf("TR_CKPT after setShape (non-BLOCK)");\n'
    "\t}\n"
    '\tif (s_trLog) app.DebugPrintf("TR_CKPT before setMipmapEnable Tile::mipmapEnable=%p id=%d", (void*)Tile::mipmapEnable, (int)tt->id);\n'
    "\tt->setMipmapEnable( Tile::mipmapEnable[tt->id] );\t// 4J added\n"
    '\tif (s_trLog) app.DebugPrintf("TR_CKPT after setMipmapEnable");\n'
    '\tif (s_trLog) app.DebugPrintf("TR_CKPT pre-switch noCulling=%d shape=%d", (int)noCulling, shape);'
)

if old not in src:
    sys.exit(f"anchor not found in {TARGET}")
src = src.replace(old, new, 1)

# Now bracket getFaceFlags / setShape / tesselateBlockInWorld in the
# SHAPE_BLOCK branch (uses the same s_trLog set above).
old2 = (
    "\t\t\t\tif( ( tt->id <= Tile::unbreakable_Id  ) ||\n"
    "\t\t\t\t\t( ( tt->id >= Tile::sand_Id ) && ( tt->id <= Tile::treeTrunk_Id ) ) )\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tfaceFlags = tt->getFaceFlags( level, x, y, z );\n"
    "\t\t\t\t}"
)
new2 = (
    "\t\t\t\tif( ( tt->id <= Tile::unbreakable_Id  ) ||\n"
    "\t\t\t\t\t( ( tt->id >= Tile::sand_Id ) && ( tt->id <= Tile::treeTrunk_Id ) ) )\n"
    "\t\t\t\t{\n"
    '\t\t\t\t\tif (s_trLog) app.DebugPrintf("TR_CKPT before getFaceFlags");\n'
    "\t\t\t\t\tfaceFlags = tt->getFaceFlags( level, x, y, z );\n"
    '\t\t\t\t\tif (s_trLog) app.DebugPrintf("TR_CKPT after getFaceFlags=%d", faceFlags);\n'
    "\t\t\t\t}"
)
if old2 not in src:
    sys.exit(f"anchor 2 not found in {TARGET}")
src = src.replace(old2, new2, 1)

old3 = (
    "\t\t\t// now we need to set the shape\n"
    "\t\t\tsetShape(tt);\n"
    "\n"
    "\t\t\tretVal = tesselateBlockInWorld( tt, x, y, z, faceFlags );"
)
new3 = (
    "\t\t\t// now we need to set the shape\n"
    '\t\t\tif (s_trLog) app.DebugPrintf("TR_CKPT before setShape (BLOCK)");\n'
    "\t\t\tsetShape(tt);\n"
    '\t\t\tif (s_trLog) app.DebugPrintf("TR_CKPT before tesselateBlockInWorld faceFlags=%d", faceFlags);\n'
    "\t\t\tretVal = tesselateBlockInWorld( tt, x, y, z, faceFlags );\n"
    '\t\t\tif (s_trLog) app.DebugPrintf("TR_CKPT after tesselateBlockInWorld retVal=%d", (int)retVal);'
)

# G5-step24: narrow inside tesselateBlockInWorld(Tile*, int, int, int, int)
# at line 4910. Bracket getColor / lightEmission / sub-call.
old4 = (
    "bool TileRenderer::tesselateBlockInWorld( Tile* tt, int x, int y, int z, int faceFlags )\n"
    "{\n"
    "\tint\t\tcol = tt->getColor( level, x, y, z );\n"
    "\tfloat\tr = ( ( col >> 16 ) & 0xff ) / 255.0f;"
)
new4 = (
    "bool TileRenderer::tesselateBlockInWorld( Tile* tt, int x, int y, int z, int faceFlags )\n"
    "{\n"
    '\tstatic int s_tbiwCount = 0;\n'
    '\tbool s_tbiwLog = (s_tbiwCount++ < 3);\n'
    '\tif (s_tbiwLog) app.DebugPrintf("TBIW_CKPT enter tt=%p id=%d xyz=%d,%d,%d ff=%d level=%p", tt, (int)tt->id, x, y, z, faceFlags, level);\n'
    '\tif (s_tbiwLog) app.DebugPrintf("TBIW_CKPT before tt->getColor");\n'
    "\tint\t\tcol = tt->getColor( level, x, y, z );\n"
    '\tif (s_tbiwLog) app.DebugPrintf("TBIW_CKPT after tt->getColor=0x%x", col);\n'
    "\tfloat\tr = ( ( col >> 16 ) & 0xff ) / 255.0f;"
)
if old4 not in src:
    sys.exit(f"anchor 4 not found in {TARGET}")
src = src.replace(old4, new4, 1)

# G5-step25: bracket tesselateBlockInWorldWithAmbienceOcclusionTexLighting entry.
old6 = (
    "bool TileRenderer::tesselateBlockInWorldWithAmbienceOcclusionTexLighting( Tile* tt, int pX, int pY, int pZ,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t float pBaseRed, float pBaseGreen,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t float pBaseBlue, int faceFlags, bool smoothShapeLighting )\n"
    "{"
)
new6 = (
    "bool TileRenderer::tesselateBlockInWorldWithAmbienceOcclusionTexLighting( Tile* tt, int pX, int pY, int pZ,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t float pBaseRed, float pBaseGreen,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t float pBaseBlue, int faceFlags, bool smoothShapeLighting )\n"
    "{\n"
    '\tstatic int s_aoCount = 0;\n'
    '\tbool s_aoLog = (s_aoCount++ < 3);\n'
    '\tif (s_aoLog) app.DebugPrintf("AO_CKPT enter tt=%p id=%d xyz=%d,%d,%d ff=%d", tt, (int)tt->id, pX, pY, pZ, faceFlags);'
)
if old6 in src:
    src = src.replace(old6, new6, 1)

# Bracket getLightColor + t->tex2 calls.
old7 = (
    "\tint\t\t\tcenterColor = getLightColor(tt,  level, pX, pY, pZ );\n"
    "\n"
    "\tTesselator* t = Tesselator::getInstance();\n"
    "\tt->tex2( 0xf000f );"
)
new7 = (
    '\tif (s_aoLog) app.DebugPrintf("AO_CKPT before getLightColor");\n'
    "\tint\t\t\tcenterColor = getLightColor(tt,  level, pX, pY, pZ );\n"
    '\tif (s_aoLog) app.DebugPrintf("AO_CKPT after getLightColor=0x%x", centerColor);\n'
    "\tTesselator* t = Tesselator::getInstance();\n"
    '\tif (s_aoLog) app.DebugPrintf("AO_CKPT before t->tex2 t=%p", t);\n'
    "\tt->tex2( 0xf000f );\n"
    '\tif (s_aoLog) app.DebugPrintf("AO_CKPT after t->tex2");'
)
if old7 in src:
    src = src.replace(old7, new7, 1)

old5 = (
    "\tif ( Tile::lightEmission[tt->id] == 0 )//4J - TODO/remove (Minecraft::useAmbientOcclusion())\n"
    "\t{\n"
    "\t\treturn tesselateBlockInWorldWithAmbienceOcclusionTexLighting( tt, x, y, z, r, g, b, faceFlags, smoothShapeLighting );\n"
    "\t}\n"
    "\telse\n"
    "\t{\n"
    "\t\treturn tesselateBlockInWorld( tt, x, y, z, r, g, b );\n"
    "\t}"
)
new5 = (
    '\tif (s_tbiwLog) app.DebugPrintf("TBIW_CKPT lightEmission ptr=%p tt->id=%d val=%d", (void*)Tile::lightEmission, (int)tt->id, (int)Tile::lightEmission[tt->id]);\n'
    "\tif ( Tile::lightEmission[tt->id] == 0 )//4J - TODO/remove (Minecraft::useAmbientOcclusion())\n"
    "\t{\n"
    '\t\tif (s_tbiwLog) app.DebugPrintf("TBIW_CKPT before AO branch (lightEmission==0)");\n'
    "\t\treturn tesselateBlockInWorldWithAmbienceOcclusionTexLighting( tt, x, y, z, r, g, b, faceFlags, smoothShapeLighting );\n"
    "\t}\n"
    "\telse\n"
    "\t{\n"
    '\t\tif (s_tbiwLog) app.DebugPrintf("TBIW_CKPT before non-AO branch (lightEmission!=0)");\n'
    "\t\treturn tesselateBlockInWorld( tt, x, y, z, r, g, b );\n"
    "\t}"
)
if old5 not in src:
    sys.exit(f"anchor 5 not found in {TARGET}")
src = src.replace(old5, new5, 1)
if old3 not in src:
    sys.exit(f"anchor 3 not found in {TARGET}")
src = src.replace(old3, new3, 1)

TARGET.write_text(src, encoding="utf-8")
print(f"patched: {TARGET}")
