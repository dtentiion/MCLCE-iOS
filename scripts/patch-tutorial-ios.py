#!/usr/bin/env python3
"""Reach the tutorial from the server player that is the local player here.

- ControllerTask reads the pad as the tutorial's own. mc->player is the
  ServerPlayer aliased as a MultiplayerLocalPlayer, so GetXboxPad would read
  whatever ServerPlayer has at LocalPlayer's m_iPad.
- PlayerList::respawn asks Minecraft::gameMode for the tutorial. There's no
  game mode object here, the tutorial is Game/MCLETutorial.cpp's.
- ServerPlayer gets LocalPlayer's onCrafted and the tutorial part of its
  handleCollectItem. The client's inventory and crafting are the server
  player's here, so this is where the client would have seen them.
- The game rules' progress goes to the local player through
  mcle_game_rule_send. The connection they're given stands in for the
  player's, which isn't a Connection.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CLIENT = REPO / "upstream" / "Minecraft.Client"
MARK = "mcle_tutorial"


def fail(msg):
    print(msg)
    sys.exit(1)


def patch(path, edits):
    """edits: (old, new, count). Keeps the file's line endings."""
    raw = path.read_bytes().decode("utf-8", errors="replace")
    crlf = "\r\n" in raw
    src = raw.replace("\r\n", "\n")
    if MARK in src or "mcle_local_player_" in src or "mcle_game_rule_send" in src:
        print(f"already patched: {path}")
        return
    for old, new, count in edits:
        n = src.count(old)
        if n != count:
            fail(f"expected {count} of {old.strip()!r} in {path}, found {n}")
        src = src.replace(old, new)
    if crlf:
        src = src.replace("\n", "\r\n")
    path.write_bytes(src.encode("utf-8"))
    print(f"patched: {path}")


patch(CLIENT / "Common" / "Tutorial" / "ControllerTask.cpp", [
    ("pMinecraft->player->GetXboxPad()", "tutorial->getPad()", 3),
    ('#include "ControllerTask.h"\n',
     '#include "ControllerTask.h"\n\n// mcle_tutorial: the tutorial\'s pad is the player\'s\n', 1),
])

patch(CLIENT / "PlayerList.cpp", [
    ('#include "PlayerList.h"\n',
     '#include "PlayerList.h"\n\nTutorial *mcle_tutorial(); // Game/MCLETutorial.cpp\n', 1),
    ("Minecraft::GetInstance()->gameMode->getTutorial()->isStateCompleted(e_Tutorial_State_Food_Bar)",
     "mcle_tutorial()->isStateCompleted(e_Tutorial_State_Food_Bar)", 1),
])

patch(CLIENT / "ServerPlayer.h", [
    ("\tvirtual void handleCollectItem(shared_ptr<ItemInstance> item);\n",
     "\tvirtual void handleCollectItem(shared_ptr<ItemInstance> item);\n"
     "#ifdef __APPLE_IOS__\n"
     "\t// LocalPlayer's, the local player is this one (mcle_local_player_on_crafted)\n"
     "\tvirtual void onCrafted(shared_ptr<ItemInstance> item);\n"
     "#endif\n", 1),
])

patch(CLIENT / "ServerPlayer.cpp", [
    ('#include "ServerPlayer.h"\n',
     '#include "ServerPlayer.h"\n'
     '#ifdef __APPLE_IOS__\n'
     '// Game/MCLETutorial.cpp, LocalPlayer\'s calls into the tutorial\n'
     'void mcle_local_player_on_crafted(Player *player, shared_ptr<ItemInstance> item);\n'
     'void mcle_local_player_collect_item(Player *player, shared_ptr<ItemInstance> item);\n'
     '#endif\n', 1),
    ("void ServerPlayer::handleCollectItem(shared_ptr<ItemInstance> item)\n"
     "{\n"
     "\tif(gameMode->getGameRules() != nullptr) gameMode->getGameRules()->onCollectItem(item);\n"
     "}\n",
     "void ServerPlayer::handleCollectItem(shared_ptr<ItemInstance> item)\n"
     "{\n"
     "\tif(gameMode->getGameRules() != nullptr) gameMode->getGameRules()->onCollectItem(item);\n"
     "#ifdef __APPLE_IOS__\n"
     "\tmcle_local_player_collect_item(this, item);\n"
     "#endif\n"
     "}\n"
     "\n"
     "#ifdef __APPLE_IOS__\n"
     "void ServerPlayer::onCrafted(shared_ptr<ItemInstance> item)\n"
     "{\n"
     "\tmcle_local_player_on_crafted(this, item);\n"
     "}\n"
     "#endif\n", 1),
])

RULE_SEND = ("rule->getConnection()->send(", "mcle_game_rule_send(rule->getConnection(), ", 1)
RULE_DECL = ("class Connection;\nclass Packet;\n"
             "void mcle_game_rule_send(Connection *connection, shared_ptr<Packet> packet); // Game/MCLETutorial.cpp\n")
patch(CLIENT / "Common" / "GameRules" / "CollectItemRuleDefinition.cpp", [
    ('#include "CollectItemRuleDefinition.h"\n', '#include "CollectItemRuleDefinition.h"\n' + RULE_DECL, 1),
    RULE_SEND,
])
patch(CLIENT / "Common" / "GameRules" / "CompleteAllRuleDefinition.cpp", [
    ('#include "CompleteAllRuleDefinition.h"\n', '#include "CompleteAllRuleDefinition.h"\n' + RULE_DECL, 1),
    RULE_SEND,
])
