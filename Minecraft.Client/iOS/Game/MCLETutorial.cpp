// The local player's tutorial. Tutorial.cpp and the tasks that use the UI are
// upstream's own: their bodies are copied out by scripts/gen-tutorial.py and
// compiled below against MCLETutorialUI, which stands in for the UIController
// calls they make and passes the popup ones to MCLETutorialPopup.cpp.
// Around that is what TutorialMode, Minecraft, LocalPlayer and
// ClientConnection do with the tutorial.

#include "MCLETutorial.h"
#include "MCLETutorialPopup.h"
#include "MCLEContainerScene.h"

#include <cstring>
#include <mutex>

#include "../../../upstream/Minecraft.World/net.minecraft.stats.h"
#include "../../../upstream/Minecraft.World/Entity.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.entity.player.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.level.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.level.tile.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.effect.h"
#include "../../../upstream/Minecraft.World/Inventory.h"
#include "../../../upstream/Minecraft.World/Material.h"
#include "../../../upstream/Minecraft.World/AABB.h"
#include "../../../upstream/Minecraft.World/LevelSettings.h"
#include "../../../upstream/Minecraft.World/FoodConstants.h"
#include "../../../upstream/Minecraft.World/EntityEvent.h"
#include "../../../upstream/Minecraft.World/SetHealthPacket.h"
#include "../../../upstream/Minecraft.World/EntityEventPacket.h"
#include "../../../upstream/Minecraft.World/UpdateMobEffectPacket.h"
#include "../../../upstream/Minecraft.World/RemoveMobEffectPacket.h"
#include "../../../upstream/Minecraft.World/SetEntityLinkPacket.h"
#include "../../../upstream/Minecraft.World/UpdateGameRuleProgressPacket.h"
#include "../../../upstream/Minecraft.World/PlayerInfoPacket.h"
#include "../../../upstream/Minecraft.World/GameEventPacket.h"
#include "../../../upstream/Minecraft.Client/MinecraftServer.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/LocalPlayer.h"
#include "../../../upstream/Minecraft.Client/MultiPlayerLocalPlayer.h"
#include "../../../upstream/Minecraft.Client/MultiPlayerLevel.h"
#include "../../../upstream/Minecraft.Client/PlayerList.h"
#include "../../../upstream/Minecraft.Client/PlayerConnection.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/ServerPlayerGameMode.h"
#include "../../../upstream/Minecraft.Client/Common/GameRules/GameRuleDefinition.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_CraftingMenu.h"
#include "../../../upstream/Minecraft.Client/Common/Tutorial/Tutorial.h"
#include "../../../upstream/Minecraft.Client/Common/Tutorial/TutorialMessage.h"
#include "../../../upstream/Minecraft.Client/Common/Tutorial/TutorialTasks.h"
#include "../../../upstream/Minecraft.Client/Common/Tutorial/TutorialConstraints.h"
#include "../../../upstream/Minecraft.Client/Common/Tutorial/TutorialHints.h"
#include "../../../upstream/Minecraft.Client/Common/Tutorial/FullTutorial.h"

extern "C" void mcle_settings_get_tutorial(unsigned char *completion, int bytes, unsigned int *special); // MCLE_iOS_Settings.mm
extern "C" void mcle_settings_set_tutorial(const unsigned char *completion, int bytes, unsigned int special);
bool mcle_container_is_open();      // MCLEContainerMenu.cpp
bool mcle_game_menu_displayed();    // MCLEGameLoop.cpp
bool mcle_game_paused();            // MCLEGameLoop.cpp
void mcle_local_player_packet(std::shared_ptr<Packet> packet); // WorldProbe/link_stubs.cpp

// The part of GAME_SETTINGS the tutorial keeps its progress in
struct MCLETutorialProfile {
    bool bSettingsChanged;
    unsigned char ucTutorialCompletion[TUTORIAL_PROFILE_STORAGE_BYTES];
    unsigned int uiSpecialTutorialBitmask;
};

namespace {

MCLETutorialProfile g_profile;
bool g_profileLoaded = false;

Tutorial *g_tutorial = nullptr;
// What the local player rode before the last SetEntityLinkPacket
bool g_wasRiding = false;

// Written back like a profile write once something changed
void saveProfile() {
    if (!g_profileLoaded || !g_profile.bSettingsChanged) return;
    g_profile.bSettingsChanged = false;
    mcle_settings_set_tutorial(g_profile.ucTutorialCompletion, TUTORIAL_PROFILE_STORAGE_BYTES,
                               g_profile.uiSpecialTutorialBitmask);
}

std::shared_ptr<ServerPlayer> localPlayer() {
    return mcle_local_player();
}

} // namespace

// ProfileManager.GetGameDefinedProfileData, the one profile there is
void *mcle_tutorial_profile_data(int /*iPad*/) {
    if (!g_profileLoaded) {
        memset(&g_profile, 0, sizeof(g_profile));
        mcle_settings_get_tutorial(g_profile.ucTutorialCompletion, TUTORIAL_PROFILE_STORAGE_BYTES,
                                   &g_profile.uiSpecialTutorialBitmask);
        g_profileLoaded = true;
    }
    return &g_profile;
}

// The UIController calls the tutorial and its tasks make
struct MCLETutorialUI {
    void SetTutorial(int, Tutorial *tutorial) { mcle_tutorial_popup_set_tutorial(tutorial); }
    void SetTutorialDescription(int, MCLETutorialPopupInfo *info) {
        // tutorial popup needs to know if a container menu is being displayed
        mcle_tutorial_popup_set_container_menu_visible(mcle_container_is_open());
        mcle_tutorial_popup_set_description(info);
    }
    void RemoveInteractSceneReference(int, UIScene *scene) { mcle_tutorial_popup_remove_interact_scene(scene); }
    void SetTutorialVisible(int, bool visible) { mcle_tutorial_popup_set_visible(visible); }
    bool IsTutorialVisible(int) { return mcle_tutorial_popup_is_visible(); }
    bool IsPauseMenuDisplayed(int) { return mcle_game_paused(); }
    bool GetMenuDisplayed(int) { return mcle_game_menu_displayed(); }
};
MCLETutorialUI mcleTutorialUI;

typedef MCLETutorialPopupInfo TutorialPopupInfo;

#define ui mcleTutorialUI
#include "../../../upstream/Minecraft.Client/iOS_Tutorial.inc"
#undef ui

// Minecraft's tutorial bits, set by FullTutorialMode and cleared when the
// player leaves the tutorial or it's finished
bool Minecraft::isTutorial() {
    return m_inFullTutorialBits > 0;
}

void Minecraft::playerStartedTutorial(int iPad) {
    // If the app doesn't think we are in a tutorial mode then just ignore this add
    if (app.GetTutorialMode()) m_inFullTutorialBits = m_inFullTutorialBits | (1 << iPad);
}

void Minecraft::playerLeftTutorial(int iPad) {
    // 4J Stu - Fix for bug that was flooding Sentient with LevelStart events
    // If the tutorial bits are already 0 then don't need to update anything
    if (m_inFullTutorialBits == 0) {
        app.SetTutorialMode(false);
        return;
    }

    m_inFullTutorialBits = m_inFullTutorialBits & ~(1 << iPad);
    if (m_inFullTutorialBits == 0) {
        app.SetTutorialMode(false);

        for (DWORD idx = 0; idx < XUSER_MAX_COUNT; ++idx) {
            if (localplayers[idx] != nullptr && level != nullptr) {
                TelemetryManager->RecordLevelStart(idx, eSen_FriendOrMatch_Playing_With_Invited_Friends,
                                                   eSen_CompeteOrCoop_Coop_and_Competitive, level->difficulty,
                                                   app.GetLocalPlayerCount(), g_NetworkManager.GetOnlinePlayerCount());
            }
        }
    }
}

Tutorial *mcle_tutorial() {
    return g_tutorial;
}

void mcle_tutorial_start() {
    if (g_tutorial) return;
    Minecraft *minecraft = Minecraft::GetInstance();
    int iPad = ProfileManager.GetPrimaryPad();
    if (app.GetTutorialMode()) {
        // FullTutorialMode
        g_tutorial = new FullTutorial(iPad);
        if (minecraft) minecraft->playerStartedTutorial(iPad);
    } else {
        // ConsoleGameMode. TrialMode is only for the trial, and the game is
        // always the full version here.
        g_tutorial = new Tutorial(iPad);
    }
    std::shared_ptr<ServerPlayer> player = localPlayer();
    g_wasRiding = player != nullptr && player->riding != nullptr;
}

void mcle_tutorial_end() {
    Minecraft *minecraft = Minecraft::GetInstance();
    // 4J Stu - Fix for #13257 - CRASH: Gameplay: Title crashed after exiting the tutorial
    // It doesn't matter if they were in the tutorial already
    if (minecraft) minecraft->playerLeftTutorial(ProfileManager.GetPrimaryPad());
    // UIController::CloseAllPlayersScenes empties the popup
    mcle_tutorial_popup_reset();
    // ~TutorialMode
    delete g_tutorial;
    g_tutorial = nullptr;
    saveProfile();
}

void mcle_tutorial_tick() {
    if (!g_tutorial) return;
    if (!g_tutorial->m_allTutorialsComplete) g_tutorial->tick();
    saveProfile();
}

bool mcle_tutorial_input_allowed(int mapping) {
    if (!g_tutorial) return true;
    return g_tutorial->m_allTutorialsComplete || g_tutorial->isInputAllowed(mapping);
}

void mcle_tutorial_start_destroy_block(Level *level, std::shared_ptr<ItemInstance> selected, int x, int y, int z) {
    if (!g_tutorial || !level) return;
    if (!g_tutorial->m_allTutorialsComplete) {
        int t = level->getTile(x, y, z);
        g_tutorial->startDestroyBlock(selected, Tile::tiles[t]);
    }
}

MCLETutorialDestroy mcle_tutorial_destroy_block(Level *level, std::shared_ptr<ItemInstance> selected, int x, int y,
                                                int z) {
    MCLETutorialDestroy destroy;
    if (!g_tutorial || !level) return destroy;
    if (!g_tutorial->m_allTutorialsComplete) {
        int t = level->getTile(x, y, z);
        g_tutorial->destroyBlock(Tile::tiles[t]);
    }
    destroy.item = selected;
    if (destroy.item != nullptr) {
        destroy.damageBefore = destroy.item->getDamageValue();
    }
    return destroy;
}

void mcle_tutorial_block_destroyed(const MCLETutorialDestroy &destroy) {
    if (!g_tutorial) return;
    if (!g_tutorial->m_allTutorialsComplete) {
        shared_ptr<ItemInstance> item = destroy.item;
        if (item != nullptr && item->isDamageableItem()) {
            int max = item->getMaxDamage();
            int damageNow = item->getDamageValue();

            if (damageNow > destroy.damageBefore && damageNow > (max / 2)) {
                g_tutorial->itemDamaged(item);
            }
        }
    }
}

MCLETutorialUse mcle_tutorial_use_item_on(Level *level, std::shared_ptr<ItemInstance> item, int x, int y, int z,
                                          bool bTestUseOnly) {
    MCLETutorialUse use;
    if (!g_tutorial) return use;
    if (!g_tutorial->m_allTutorialsComplete) {
        g_tutorial->useItemOn(level, item, x, y, z, bTestUseOnly);

        if (!bTestUseOnly) {
            if (item != nullptr) {
                use.haveItem = true;
                use.itemCount = item->count;
            }
        }
    }
    return use;
}

void mcle_tutorial_used_item_on(const MCLETutorialUse &use, bool result, std::shared_ptr<ItemInstance> item,
                                bool bTestUseOnly) {
    if (!g_tutorial) return;
    if (!bTestUseOnly) {
        if (!g_tutorial->m_allTutorialsComplete) {
            if (result && use.haveItem && use.itemCount > item->count) {
                g_tutorial->useItemOn(item);
            }
        }
    }
}

void mcle_tutorial_attack(std::shared_ptr<Player> player, std::shared_ptr<Entity> entity) {
    if (!g_tutorial) return;
    if (!g_tutorial->m_allTutorialsComplete) g_tutorial->attack(player, entity);
}

void mcle_tutorial_selected_item_changed(std::shared_ptr<ItemInstance> item) {
    // 4J Stu - For the tutorial we want to be able to record what items we are using so that we can give hints
    if (g_tutorial) g_tutorial->onSelectedItemChanged(item);
}

bool mcle_tutorial_can_move_to_position(double xo, double yo, double zo, double xt, double yt, double zt) {
    if (!g_tutorial) return true;
    return g_tutorial->canMoveToPosition(xo, yo, zo, xt, yt, zt);
}

// LocalPlayer::onCrafted
void mcle_local_player_on_crafted(Player *player, std::shared_ptr<ItemInstance> item) {
    if (!g_tutorial || player == nullptr || player != localPlayer().get()) return;
    g_tutorial->onCrafted(item);
}

// LocalPlayer::handleCollectItem's tutorial part
void mcle_local_player_collect_item(Player *player, std::shared_ptr<ItemInstance> item) {
    if (!g_tutorial || player == nullptr || player != localPlayer().get()) return;
    if (item != nullptr) {
        unsigned int itemCountAnyAux = 0;
        unsigned int itemCountThisAux = 0;
        for (unsigned int k = 0; k < player->inventory->items.length; ++k) {
            if (player->inventory->items[k] != nullptr) {
                // do they have the item
                if (player->inventory->items[k]->id == item->id) {
                    unsigned int quantity = player->inventory->items[k]->GetCount();

                    itemCountAnyAux += quantity;

                    if (player->inventory->items[k]->getAuxValue() == item->getAuxValue()) {
                        itemCountThisAux += quantity;
                    }
                }
            }
        }
        g_tutorial->onTake(item, itemCountAnyAux, itemCountThisAux);
    }
}

namespace {

// ClientConnection::handleUpdateGameRuleProgressPacket
void handleUpdateGameRuleProgressPacket(const std::shared_ptr<UpdateGameRuleProgressPacket> &packet) {
    LPCWSTR string = app.GetGameRulesString(packet->m_messageId);
    if (string != nullptr) {
        wstring message(string);
        message = GameRuleDefinition::generateDescriptionString(packet->m_definitionType, message,
                                                                 packet->m_data.data, packet->m_data.length);
        if (g_tutorial != nullptr) {
            g_tutorial->setMessage(message, packet->m_icon, packet->m_auxValue);
        }
    }
    // If this rule has a data tag associated with it, then we save that in user profile data
    if (packet->m_dataTag > 0 && packet->m_dataTag <= 32) {
        app.DebugPrintf("handleUpdateGameRuleProgressPacket: Data tag is in range, so updating profile data\n");
        // CMinecraftApp::SetSpecialTutorialCompletionFlag
        int index = packet->m_dataTag - 1;
        MCLETutorialProfile *profile = static_cast<MCLETutorialProfile *>(mcle_tutorial_profile_data(0));
        if (index >= 0 && index < 32) {
            profile->uiSpecialTutorialBitmask |= (1 << index);
            profile->bSettingsChanged = true;
        }
    }
    delete[] packet->m_data.data;
    packet->m_data.data = nullptr;
    packet->m_data.length = 0;
}

} // namespace

void mcle_tutorial_local_player_packet(std::shared_ptr<Packet> packet) {
    std::shared_ptr<ServerPlayer> player = localPlayer();
    if (std::shared_ptr<UpdateGameRuleProgressPacket> progress =
            std::dynamic_pointer_cast<UpdateGameRuleProgressPacket>(packet)) {
        handleUpdateGameRuleProgressPacket(progress);
        saveProfile();
        return;
    }
    if (!g_tutorial || !player) return;

    if (std::shared_ptr<SetHealthPacket> health = std::dynamic_pointer_cast<SetHealthPacket>(packet)) {
        // ClientConnection::handleSetHealth: we need food
        if (health->food < FoodConstants::HEAL_LEVEL - 1) {
            // MultiPlayerGameMode::hasInfiniteItems
            if (player->gameMode != nullptr && !player->gameMode->isCreative()) {
                g_tutorial->changeTutorialState(e_Tutorial_State_Food_Bar);
            }
        }
    } else if (std::shared_ptr<EntityEventPacket> event = std::dynamic_pointer_cast<EntityEventPacket>(packet)) {
        // handleEntityEvent: Player::handleEntityEvent completes the item,
        // and MultiplayerLocalPlayer::completeUsingItem tells the tutorial
        if (event->entityId == player->entityId && event->eventId == EntityEvent::USE_ITEM_COMPLETE) {
            shared_ptr<ItemInstance> useItem = player->getUseItem();
            if (useItem != nullptr) g_tutorial->completeUsingItem(useItem);
        }
    } else if (std::shared_ptr<UpdateMobEffectPacket> effect = std::dynamic_pointer_cast<UpdateMobEffectPacket>(packet)) {
        // handleUpdateMobEffect: MultiplayerLocalPlayer::onEffectAdded / onEffectUpdated
        if (effect->entityId == player->entityId) g_tutorial->onEffectChanged(MobEffect::effects[effect->effectId]);
    } else if (std::shared_ptr<RemoveMobEffectPacket> removed = std::dynamic_pointer_cast<RemoveMobEffectPacket>(packet)) {
        // handleRemoveMobEffect: MultiplayerLocalPlayer::onEffectRemoved
        if (removed->entityId == player->entityId) {
            g_tutorial->onEffectChanged(MobEffect::effects[removed->effectId], true);
        }
    } else if (std::shared_ptr<SetEntityLinkPacket> link = std::dynamic_pointer_cast<SetEntityLinkPacket>(packet)) {
        // handleSetEntityLink: MultiplayerLocalPlayer::ride
        if (link->type == SetEntityLinkPacket::RIDING && link->sourceId == player->entityId) {
            bool wasRiding = g_wasRiding;
            bool isRiding = player->riding != nullptr;
            g_wasRiding = isRiding;
            if (wasRiding && !isRiding) {
                g_tutorial->changeTutorialState(e_Tutorial_State_Gameplay);
            } else if (!wasRiding && isRiding) {
                g_tutorial->onRideEntity(player->riding);
            }
        }
    }
}

namespace {
// The connection the game rules are given. Nothing is called on it, it's
// only told apart from no connection.
struct alignas(void *) StandInConnection {
    char bytes[sizeof(void *)];
};
StandInConnection g_gameRuleConnection = {};
} // namespace

Connection *mcle_game_rule_connection() {
    return reinterpret_cast<Connection *>(&g_gameRuleConnection);
}

// There are no other connections here, it's always the local player's
void mcle_game_rule_send(Connection *connection, std::shared_ptr<Packet> packet) {
    if (connection == mcle_game_rule_connection()) mcle_local_player_packet(packet);
}

// PlayerConnection::handlePlayerInfo for the host editing itself
void mcle_tutorial_player_info(unsigned int playerPrivileges) {
    std::shared_ptr<ServerPlayer> serverPlayer = localPlayer();
    if (serverPlayer == nullptr || serverPlayer->gameMode == nullptr) return;

    unsigned int origPrivs = serverPlayer->getAllPlayerGamePrivileges();

    bool cheats = app.GetGameHostOption(eGameHostOption_CheatsEnabled) != 0;

    GameType *gameType = Player::getPlayerGamePrivilege(playerPrivileges, Player::ePlayerGamePrivilege_CreativeMode)
                             ? GameType::CREATIVE
                             : GameType::SURVIVAL;
    gameType = LevelSettings::validateGameType(gameType->getId());
    if (serverPlayer->gameMode->getGameModeForPlayer() != gameType) {
        serverPlayer->setPlayerGamePrivilege(
            Player::ePlayerGamePrivilege_CreativeMode,
            Player::getPlayerGamePrivilege(playerPrivileges, Player::ePlayerGamePrivilege_CreativeMode));
        serverPlayer->gameMode->setGameModeForPlayer(gameType);
        serverPlayer->connection->send(
            std::make_shared<GameEventPacket>(GameEventPacket::CHANGE_GAME_MODE, gameType->getId()));
    }
    if (cheats) {
        // Editing self
        bool canBeInvisible = Player::getPlayerGamePrivilege(origPrivs, Player::ePlayerGamePrivilege_CanToggleInvisible) != 0;
        if (canBeInvisible) {
            serverPlayer->setPlayerGamePrivilege(
                Player::ePlayerGamePrivilege_Invisible,
                Player::getPlayerGamePrivilege(playerPrivileges, Player::ePlayerGamePrivilege_Invisible));
        }
        if (canBeInvisible) {
            serverPlayer->setPlayerGamePrivilege(
                Player::ePlayerGamePrivilege_Invulnerable,
                Player::getPlayerGamePrivilege(playerPrivileges, Player::ePlayerGamePrivilege_Invulnerable));
        }

        bool inCreativeMode = Player::getPlayerGamePrivilege(origPrivs, Player::ePlayerGamePrivilege_CreativeMode) != 0;
        if (!inCreativeMode) {
            bool canFly = Player::getPlayerGamePrivilege(origPrivs, Player::ePlayerGamePrivilege_CanToggleFly);
            bool canChangeHunger =
                Player::getPlayerGamePrivilege(origPrivs, Player::ePlayerGamePrivilege_CanToggleClassicHunger);

            if (canFly) {
                serverPlayer->setPlayerGamePrivilege(
                    Player::ePlayerGamePrivilege_CanFly,
                    Player::getPlayerGamePrivilege(playerPrivileges, Player::ePlayerGamePrivilege_CanFly));
            }
            if (canChangeHunger) {
                serverPlayer->setPlayerGamePrivilege(
                    Player::ePlayerGamePrivilege_ClassicHunger,
                    Player::getPlayerGamePrivilege(playerPrivileges, Player::ePlayerGamePrivilege_ClassicHunger));
            }
        }
    }

    if (PlayerList *players = MinecraftServer::getPlayerList()) {
        players->broadcastAll(std::make_shared<PlayerInfoPacket>(serverPlayer));
    }
}

IUIScene_CraftingMenu *mcle_tutorial_crafting_scene(UIScene *scene) {
    MCLEContainerScene *current = mcle_container_current_scene();
    if (scene == nullptr || current == nullptr || mcle_ui_scene(current) != scene) return nullptr;
    return current->craftingMenu();
}

void mcle_tutorial_show_popup(bool show) {
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    if (g_tutorial) g_tutorial->showTutorialPopup(show);
}
