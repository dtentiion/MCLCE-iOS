#!/usr/bin/env python3
"""Client side effects of a few server packets.

There's no client level on iOS, so packets the server sends the local
player never arrive. A few of them only make particles and sounds on the
client:

- AnimatePacket CRITICAL_HIT / MAGIC_CRITICAL_HIT from ServerPlayer::crit
  and magicCrit: ClientConnection::handleAnimate adds a CritParticle.
- TakeItemEntityPacket from LivingEntity::take: handleTakeItemEntity plays
  the pickup pop (the orb sound for xp) and adds the TakeAnimationParticle
  that flies the item into whoever took it.
- EntityEventPacket from ServerLevel::broadcastEntityEvent:
  handleEntityEvent's hearts, villager and witch particles, the zombie
  cure sound and the fireworks explosion.

Game/MCLEGameLoop.cpp does those right where the packets are sent.

The server's own entities are the client's too, so in the local player's
level (mcle_client_level, Game/MCLEClientEffects.cpp) a few also do what
only their client copies do:

- LightningBolt::tick flashes the sky. The game loop counts skyFlashTime
  down like Minecraft::tick.
- FireworksRocketEntity::tick leaves the rocket's trail of sparks.
- EnderDragon::aiStep plays the wing flaps and growls, and breathes while
  landing and sitting.
- BaseMobSpawner::tick makes the spawner's flames and smoke and spins the
  mob in the cage, for tile spawners and spawner minecarts. The spin follows
  the client's own spawnDelay, which only the minecart's entity event resets
  (mcle_client_entity_event).

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UP = REPO / "upstream"
MARKER = "mcle_client_"


def patch(path, edits, marker=MARKER):
    src = path.read_text(encoding="utf-8", errors="replace")
    if marker in src:
        print(f"already patched: {path}")
        return
    nl = "\r\n" if "\r\n" in src else "\n"
    for old, new in edits:
        old = old.replace("\n", nl)
        new = new.replace("\n", nl)
        if src.count(old) != 1:
            print(f"not found once in {path}: {old.splitlines()[0]}")
            sys.exit(1)
        src = src.replace(old, new)
    path.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {path}")


DECL = ("#ifdef __APPLE_IOS__\n"
        "void mcle_client_crit(shared_ptr<Entity> entity, bool magic);\n"
        "void mcle_client_take(shared_ptr<Entity> item, shared_ptr<Entity> taker);\n"
        "#endif\n\n")

CRIT = ("\tgetLevel()->getTracker()->broadcastAndSend(shared_from_this(), "
        "std::make_shared<AnimatePacket>(entity, AnimatePacket::{kind}));\n")
patch(UP / "Minecraft.Client" / "ServerPlayer.cpp", [
    ("void ServerPlayer::crit(shared_ptr<Entity> entity)\n",
     DECL + "void ServerPlayer::crit(shared_ptr<Entity> entity)\n"),
    (CRIT.format(kind="CRITICAL_HIT"),
     CRIT.format(kind="CRITICAL_HIT") +
     "#ifdef __APPLE_IOS__\n\tmcle_client_crit(entity, false);\n#endif\n"),
    (CRIT.format(kind="MAGIC_CRITICAL_HIT"),
     CRIT.format(kind="MAGIC_CRITICAL_HIT") +
     "#ifdef __APPLE_IOS__\n\tmcle_client_crit(entity, true);\n#endif\n"),
])

TAKE_IF = "\tif (!e->removed && !level->isClientSide)\n\t{\n\t\tEntityTracker *entityTracker"
patch(UP / "Minecraft.World" / "LivingEntity.cpp", [
    ("void LivingEntity::take(shared_ptr<Entity> e, int orgCount)\n",
     DECL + "void LivingEntity::take(shared_ptr<Entity> e, int orgCount)\n"),
    (TAKE_IF,
     "\tif (!e->removed && !level->isClientSide)\n\t{\n"
     "#ifdef __APPLE_IOS__\n"
     "\t\tif (e->instanceof(eTYPE_ITEMENTITY) || e->instanceof(eTYPE_ARROW) || e->instanceof(eTYPE_EXPERIENCEORB))\n"
     "\t\t\tmcle_client_take(e, shared_from_this());\n"
     "#endif\n"
     "\t\tEntityTracker *entityTracker"),
])

EVENT = "\tserver->getLevel(dimension->id)->getTracker()->broadcastAndSend(e, p);\n}\n"
patch(UP / "Minecraft.Client" / "ServerLevel.cpp", [
    ("void ServerLevel::broadcastEntityEvent(shared_ptr<Entity> e, byte event)\n",
     "#ifdef __APPLE_IOS__\n"
     "void mcle_client_entity_event(shared_ptr<Entity> e, byte event);\n"
     "#endif\n\n"
     "void ServerLevel::broadcastEntityEvent(shared_ptr<Entity> e, byte event)\n"),
    (EVENT,
     "\tserver->getLevel(dimension->id)->getTracker()->broadcastAndSend(e, p);\n"
     "#ifdef __APPLE_IOS__\n"
     "\tmcle_client_entity_event(e, event);\n"
     "#endif\n"
     "}\n"),
])

CLIENT_LEVEL = ("#ifdef __APPLE_IOS__\n"
                "bool mcle_client_level(Level *level);\n"
                "#endif\n\n")

patch(UP / "Minecraft.World" / "LightningBolt.cpp", [
    ("void LightningBolt::tick()\n", CLIENT_LEVEL + "void LightningBolt::tick()\n"),
    ("\t\telse\n\t\t{\n\t\t\tdouble r = 3;\n",
     "\t\telse\n\t\t{\n"
     "#ifdef __APPLE_IOS__\n"
     "\t\t\t// the client's bolt, which here is the server's\n"
     "\t\t\tif (mcle_client_level(level)) level->skyFlashTime = 2;\n"
     "#endif\n"
     "\t\t\tdouble r = 3;\n"),
])

SPARK = "\tif (level->isClientSide && (life % 2) < 2)\n"
patch(UP / "Minecraft.World" / "FireworksRocketEntity.cpp", [
    ("void FireworksRocketEntity::tick()\n", CLIENT_LEVEL + "void FireworksRocketEntity::tick()\n"),
    (SPARK,
     "#ifdef __APPLE_IOS__\n"
     "\t// the client's rocket, which here is the server's\n"
     "\tif ((level->isClientSide || mcle_client_level(level)) && (life % 2) < 2)\n"
     "#else\n" + SPARK + "#endif\n"),
])

patch(UP / "Minecraft.World" / "EnderDragon.cpp", [
    ("void EnderDragon::aiStep()\n", CLIENT_LEVEL + "void EnderDragon::aiStep()\n"),
    ("\t\tsetHealth(getHealth());\n",
     "\t\tsetHealth(getHealth());\n"
     "#ifdef __APPLE_IOS__\n"
     "\t}\n"
     "\t// the client's wing flaps and growls, from the server's dragon here\n"
     "\tif (level->isClientSide || mcle_client_level(level))\n"
     "\t{\n"
     "#endif\n"),
    ("\tif (level->isClientSide)\n\t{\n\t\tif (lSteps > 0)\n",
     "#ifdef __APPLE_IOS__\n"
     "\t// and its breath. Only a client's dragon ever has lSteps.\n"
     "\tif (level->isClientSide || mcle_client_level(level))\n"
     "#else\n"
     "\tif (level->isClientSide)\n"
     "#endif\n"
     "\t{\n\t\tif (lSteps > 0)\n"),
    ("\telse\n\t{\n\t\tdouble xdd = xTarget - x;\n",
     "#ifdef __APPLE_IOS__\n"
     "\tif (!level->isClientSide)\n"
     "#else\n"
     "\telse\n"
     "#endif\n"
     "\t{\n\t\tdouble xdd = xTarget - x;\n"),
])

patch(UP / "Minecraft.World" / "BaseMobSpawner.h", [
    ("public:\n\tBaseMobSpawner();\n",
     "#ifdef __APPLE_IOS__\n"
     "public:\n"
     "\t// The client's own spawnDelay, for the client half of tick the\n"
     "\t// server's spawner does here\n"
     "\tint clientSpawnDelay = 0;\n"
     "\tbool clientSpawnDelaySet = false;\n"
     "\tvoid clientTick();\n"
     "\tvoid clientEventTriggered(int id);\n"
     "#endif\n\n"
     "public:\n\tBaseMobSpawner();\n"),
], marker="clientSpawnDelay")

patch(UP / "Minecraft.World" / "BaseMobSpawner.cpp", [
    ("void BaseMobSpawner::tick()\n", CLIENT_LEVEL + "void BaseMobSpawner::tick()\n"),
    ("\t\treturn;\n\t}\n\n\tif (getLevel()->isClientSide)\n",
     "\t\treturn;\n\t}\n\n"
     "#ifdef __APPLE_IOS__\n"
     "\t// the client's spawner, which here is the server's\n"
     "\tif (!getLevel()->isClientSide && mcle_client_level(getLevel())) clientTick();\n"
     "#endif\n\n"
     "\tif (getLevel()->isClientSide)\n"),
    ("BaseMobSpawner::SpawnData *BaseMobSpawner::getNextSpawnData()\n",
     "#ifdef __APPLE_IOS__\n"
     "// tick's client half. The client's spawnDelay starts as the Delay in the\n"
     "// tile entity data it gets, the server's then.\n"
     "void BaseMobSpawner::clientTick()\n"
     "{\n"
     "\tif (!clientSpawnDelaySet)\n"
     "\t{\n"
     "\t\tclientSpawnDelay = spawnDelay;\n"
     "\t\tclientSpawnDelaySet = true;\n"
     "\t}\n"
     "\tdouble xP = getX() + getLevel()->random->nextFloat();\n"
     "\tdouble yP = getY() + getLevel()->random->nextFloat();\n"
     "\tdouble zP = getZ() + getLevel()->random->nextFloat();\n"
     "\tgetLevel()->addParticle(eParticleType_smoke, xP, yP, zP, 0, 0, 0);\n"
     "\tgetLevel()->addParticle(eParticleType_flame, xP, yP, zP, 0, 0, 0);\n"
     "\n"
     "\tif (clientSpawnDelay > 0) clientSpawnDelay--;\n"
     "\toSpin = spin;\n"
     "\tspin = static_cast<int>(spin + 1000 / (clientSpawnDelay + 200.0f)) % 360;\n"
     "}\n"
     "\n"
     "// onEventTriggered on the client. Only the spawner minecart's entity event\n"
     "// gets there, a tile spawner's tile event isn't sent (doTileEvent is false\n"
     "// on the server).\n"
     "void BaseMobSpawner::clientEventTriggered(int id)\n"
     "{\n"
     "\tif (id == EVENT_SPAWN)\n"
     "\t{\n"
     "\t\tclientSpawnDelay = minSpawnDelay;\n"
     "\t\tclientSpawnDelaySet = true;\n"
     "\t}\n"
     "}\n"
     "#endif\n\n"
     "BaseMobSpawner::SpawnData *BaseMobSpawner::getNextSpawnData()\n"),
])
