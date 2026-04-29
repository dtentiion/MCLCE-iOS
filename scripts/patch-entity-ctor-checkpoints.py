#!/usr/bin/env python3
"""F3 hunt: bracket Entity::Entity, Entity::_init, LivingEntity::LivingEntity,
Player::Player ctor bodies with single enter/exit checkpoints so we can
see which ctor in the chain null-derefs when ServerPlayer is built.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
WORLD = REPO_ROOT / "upstream" / "Minecraft.World"

ENTITY = WORLD / "Entity.cpp"
LIVING = WORLD / "LivingEntity.cpp"
PLAYER = WORLD / "Player.cpp"

def patch(target, marker, edits):
    if not target.exists():
        sys.exit(f"missing: {target}")
    src = target.read_text(encoding="utf-8", errors="replace")
    if marker in src:
        print(f"already patched: {target}")
        return
    out = src
    for anchor, replacement in edits:
        if anchor not in out:
            sys.exit(f"anchor not found in {target}: {anchor[:80]!r}")
        out = out.replace(anchor, replacement, 1)
    target.write_text(out, encoding="utf-8", newline="\n")
    print(f"patched {target}")

# Entity::_init body (called first inside Entity::Entity)
patch(ENTITY, "ENT_CKPT", [
    (
        "void Entity::_init(bool useSmallId, Level *level)\n{\n"
        "\t// 4J - changed to assign two different types of ids.",
        "void Entity::_init(bool useSmallId, Level *level)\n{\n"
        '\tapp.DebugPrintf("ENT_CKPT _init enter useSmallId=%d level=%p", (int)useSmallId, level);\n'
        "\t// 4J - changed to assign two different types of ids.",
    ),
    (
        "\trandom = new Random();",
        '\tapp.DebugPrintf("ENT_CKPT _init before AABB::newPermanent");\n'
        "\t// (AABB already created above; reusing this anchor as a marker)\n"
        "\trandom = new Random();\n"
        '\tapp.DebugPrintf("ENT_CKPT _init random=%p", random);',
    ),
    (
        "\tif( useSmallId )\n\t{\n\t\tentityData = std::make_shared<SynchedEntityData>();\n\t}",
        '\tapp.DebugPrintf("ENT_CKPT _init before SynchedEntityData");\n'
        "\tif( useSmallId )\n\t{\n\t\tentityData = std::make_shared<SynchedEntityData>();\n\t}",
    ),
    (
        "\tif( useSmallId )\n\t{\n\t\tuuid = L\"ent\" + Mth::createInsecureUUID(random);\n\t}",
        '\tapp.DebugPrintf("ENT_CKPT _init before createInsecureUUID");\n'
        "\tif( useSmallId )\n\t{\n\t\tuuid = L\"ent\" + Mth::createInsecureUUID(random);\n\t}\n"
        '\tapp.DebugPrintf("ENT_CKPT _init done");',
    ),
    # Entity::Entity body
    (
        "Entity::Entity(Level *level, bool useSmallId)\t// 4J - added useSmallId parameter\n{\n"
        "\tMemSect(16);\n"
        "\t_init(useSmallId, level);\n"
        "\tMemSect(0);",
        "Entity::Entity(Level *level, bool useSmallId)\t// 4J - added useSmallId parameter\n{\n"
        '\tapp.DebugPrintf("ENT_CKPT ctor enter level=%p", level);\n'
        "\tMemSect(16);\n"
        "\t_init(useSmallId, level);\n"
        "\tMemSect(0);\n"
        '\tapp.DebugPrintf("ENT_CKPT ctor _init returned");',
    ),
    (
        "\tif( entityData )\n\t{\n\t\tentityData->define(DATA_SHARED_FLAGS_ID,",
        '\tapp.DebugPrintf("ENT_CKPT ctor before entityData->define block (entityData=%p)", entityData.get());\n'
        "\tif( entityData )\n\t{\n\t\tentityData->define(DATA_SHARED_FLAGS_ID,",
    ),
    (
        "\t//this->defineSynchedData();\n}",
        '\tapp.DebugPrintf("ENT_CKPT ctor done");\n'
        "\t//this->defineSynchedData();\n}",
    ),
])

# LivingEntity::LivingEntity body
patch(LIVING, "LE_CKPT", [
    (
        "LivingEntity::LivingEntity( Level* level) : Entity(level)\n{\n"
        "\tMemSect(56);\n"
        "\t_init();\n"
        "\tMemSect(0);",
        "LivingEntity::LivingEntity( Level* level) : Entity(level)\n{\n"
        '\tapp.DebugPrintf("LE_CKPT ctor enter level=%p", level);\n'
        "\tMemSect(56);\n"
        "\t_init();\n"
        "\tMemSect(0);\n"
        '\tapp.DebugPrintf("LE_CKPT ctor _init done");',
    ),
    (
        "\tfootSize = 0.5f;\n}",
        "\tfootSize = 0.5f;\n"
        '\tapp.DebugPrintf("LE_CKPT ctor done");\n}',
    ),
])

# Player::Player body
patch(PLAYER, "PL_CKPT", [
    (
        "Player::Player(Level *level, const wstring &name) : LivingEntity( level )\n{\n"
        "\t// 4J Stu - This function call had to be moved here from the Entity ctor to ensure that\n"
        "\t// the derived version of the function is called\n"
        "\tthis->defineSynchedData();",
        "Player::Player(Level *level, const wstring &name) : LivingEntity( level )\n{\n"
        '\tapp.DebugPrintf("PL_CKPT ctor enter level=%p", level);\n'
        "\t// 4J Stu - This function call had to be moved here from the Entity ctor to ensure that\n"
        "\t// the derived version of the function is called\n"
        '\tapp.DebugPrintf("PL_CKPT ctor before defineSynchedData");\n'
        "\tthis->defineSynchedData();\n"
        '\tapp.DebugPrintf("PL_CKPT ctor after defineSynchedData");',
    ),
    (
        "\t_init();\n"
        "\tMemSect(11);\n"
        "\tinventoryMenu = new InventoryMenu(inventory, !level->isClientSide, this);\n"
        "\tMemSect(0);",
        '\tapp.DebugPrintf("PL_CKPT ctor before _init");\n'
        "\t_init();\n"
        '\tapp.DebugPrintf("PL_CKPT ctor after _init inventory=%p", inventory.get());\n'
        "\tMemSect(11);\n"
        '\tapp.DebugPrintf("PL_CKPT ctor before InventoryMenu");\n'
        "\tinventoryMenu = new InventoryMenu(inventory, !level->isClientSide, this);\n"
        "\tMemSect(0);\n"
        '\tapp.DebugPrintf("PL_CKPT ctor after InventoryMenu");',
    ),
    (
        "\tPos *spawnPos = level->getSharedSpawnPos();\n"
        "\tmoveTo(spawnPos->x + 0.5, spawnPos->y + 1, spawnPos->z + 0.5, 0, 0);",
        '\tapp.DebugPrintf("PL_CKPT ctor before getSharedSpawnPos");\n'
        "\tPos *spawnPos = level->getSharedSpawnPos();\n"
        '\tapp.DebugPrintf("PL_CKPT ctor before moveTo");\n'
        "\tmoveTo(spawnPos->x + 0.5, spawnPos->y + 1, spawnPos->z + 0.5, 0, 0);\n"
        '\tapp.DebugPrintf("PL_CKPT ctor after moveTo");',
    ),
    (
        "\tsetUUID(name);\n"
        "#endif\n"
        "}",
        '\tapp.DebugPrintf("PL_CKPT ctor before setUUID");\n'
        "\tsetUUID(name);\n"
        '\tapp.DebugPrintf("PL_CKPT ctor after setUUID");\n'
        "#endif\n"
        '\tapp.DebugPrintf("PL_CKPT ctor done");\n'
        "}",
    ),
])
