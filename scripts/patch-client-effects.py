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
Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UP = REPO / "upstream"
MARKER = "mcle_client_"


def patch(path, edits):
    src = path.read_text(encoding="utf-8", errors="replace")
    if MARKER in src:
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

