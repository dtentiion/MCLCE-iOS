#pragma once

// The local player's tutorial. Upstream it belongs to the game mode
// ClientConnection::handleLogin makes, FullTutorialMode in the tutorial and
// ConsoleGameMode otherwise, both TutorialModes. There's no game mode object
// here, so the tutorial is kept on its own and the calls TutorialMode and
// the client make into it are made from the same points in the port's
// versions of them. Everything here runs with the sim tick lock held.

#include <memory>

class Tutorial;
class UIScene;
class ItemInstance;
class Entity;
class Player;
class Level;
class Packet;
class Connection;
class IUIScene_CraftingMenu;

// TutorialMode::getTutorial, null outside a world
Tutorial *mcle_tutorial();

// handleLogin's new game mode for the primary pad, and its deletion when the
// level goes (Minecraft::setLevel(nullptr) / removeLocalPlayer)
void mcle_tutorial_start();
void mcle_tutorial_end();

// TutorialMode::tick, after MultiPlayerGameMode::tick
void mcle_tutorial_tick();

// TutorialMode::isInputAllowed (MultiPlayerGameMode's allows everything)
bool mcle_tutorial_input_allowed(int mapping);

// TutorialMode::startDestroyBlock's part before MultiPlayerGameMode's
void mcle_tutorial_start_destroy_block(Level *level, std::shared_ptr<ItemInstance> selected, int x, int y, int z);

// TutorialMode::destroyBlock around MultiPlayerGameMode::destroyBlock: the
// block's hint while it's still there, then the tool worn past half way
struct MCLETutorialDestroy {
    std::shared_ptr<ItemInstance> item;
    int damageBefore = 0;
};
MCLETutorialDestroy mcle_tutorial_destroy_block(Level *level, std::shared_ptr<ItemInstance> selected, int x, int y,
                                                int z);
void mcle_tutorial_block_destroyed(const MCLETutorialDestroy &destroy);

// TutorialMode::useItemOn around MultiPlayerGameMode::useItemOn
struct MCLETutorialUse {
    bool haveItem = false;
    int itemCount = 0;
};
MCLETutorialUse mcle_tutorial_use_item_on(Level *level, std::shared_ptr<ItemInstance> item, int x, int y, int z,
                                          bool bTestUseOnly);
void mcle_tutorial_used_item_on(const MCLETutorialUse &use, bool result, std::shared_ptr<ItemInstance> item,
                                bool bTestUseOnly);

// TutorialMode::attack, before MultiPlayerGameMode's
void mcle_tutorial_attack(std::shared_ptr<Player> player, std::shared_ptr<Entity> entity);

// Minecraft::tick after the hotbar scrolls
void mcle_tutorial_selected_item_changed(std::shared_ptr<ItemInstance> item);

// MultiplayerLocalPlayer::tick: whether the move the tick made is allowed
bool mcle_tutorial_can_move_to_position(double xo, double yo, double zo, double xt, double yt, double zt);

// The packets sent to the local player that ClientConnection hands to the
// tutorial: health, finished using an item, effects, riding and the game
// rules' progress
void mcle_tutorial_local_player_packet(std::shared_ptr<Packet> packet);

// LocalPlayer::onCrafted and handleCollectItem, for the ServerPlayer that is
// the local player here (scripts/patch-tutorial-ios.py)
void mcle_local_player_on_crafted(Player *player, std::shared_ptr<ItemInstance> item);
void mcle_local_player_collect_item(Player *player, std::shared_ptr<ItemInstance> item);

// The connection the player's game rules send their progress on, which goes
// to the local player like PlayerConnection::send does
Connection *mcle_game_rule_connection();
void mcle_game_rule_send(Connection *connection, std::shared_ptr<Packet> packet);

// ChangeStateConstraint's PlayerInfoPacket, PlayerConnection::handlePlayerInfo
// for the local player
void mcle_tutorial_player_info(unsigned int playerPrivileges);

// XuiCraftingTask's cast of the tutorial's scene to the crafting menu
IUIScene_CraftingMenu *mcle_tutorial_crafting_scene(UIScene *scene);

// The profile's GAME_SETTINGS block the completion bits are kept in
void *mcle_tutorial_profile_data(int iPad);
