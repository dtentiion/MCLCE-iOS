// UIScene_InGameInfoMenu, and the UIScene_InGameHostOptionsMenu and
// UIScene_InGamePlayerOptionsMenu the host opens over it on the container
// scenes' stack. The scenes follow upstream's, with the INetworkPlayer and
// g_NetworkManager calls answered for the one network player in an offline
// game: the host, small id 0, on this system. The packets they send go to
// the host's PlayerConnection handlers. UIScene_TeleportMenu isn't here, the
// host options only have its buttons with more than one player.

#include "MCLEInGameInfoMenu.h"
#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"
#include "MCLEPlayerInfo.h"
#include "MCLETutorial.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../../../upstream/Minecraft.World/net.minecraft.world.entity.player.h"
#include "../../../upstream/Minecraft.World/ServerSettingsChangedPacket.h"
#include "../../../upstream/Minecraft.World/SoundTypes.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"

namespace {

// INetworkPlayer, the host's
struct HostNetworkPlayer {
    BYTE GetSmallId() { return 0; }
    bool IsHost() { return true; }
    bool IsLocal() { return true; }
    int GetUserIndex() { return 0; }
    // No voice chat
    bool HasVoice() { return false; }
    bool IsSameSystem(HostNetworkPlayer *) { return true; }
    // Windows64's host has the profile's gamertag
    std::wstring GetDisplayName() { return ProfileManager.GetDisplayName(0); }
};
HostNetworkPlayer g_hostNetworkPlayer;

// g_NetworkManager in an offline game with only the host in it
struct OfflineNetworkManager {
    bool IsLocalGame() { return true; }
    DWORD GetPlayerCount() { return 1; }
    HostNetworkPlayer *GetPlayerByIndex(DWORD i) { return i == 0 ? &g_hostNetworkPlayer : nullptr; }
    HostNetworkPlayer *GetPlayerBySmallId(BYTE smallId) { return smallId == 0 ? &g_hostNetworkPlayer : nullptr; }
    HostNetworkPlayer *GetLocalPlayerByUserIndex(int userIndex) {
        return userIndex == 0 ? &g_hostNetworkPlayer : nullptr;
    }
    HostNetworkPlayer *GetHostPlayer() { return &g_hostNetworkPlayer; }
};
OfflineNetworkManager g_network;

// InGamePlayerOptionsInitData
struct PlayerOptionsInitData {
    int iPad;
    BYTE networkSmallId;
    unsigned int playerPrivileges;
};

// UIControl_CheckBox. IsChecked reads the movie's Checked property; here it
// follows what the movie was told and what it reports back.
struct MCLEUICheckBox {
    std::string path;
    std::wstring label;
    int id = -1;
    bool checked = false;
    bool enabled = true;
    // FJ_CheckBox's own enabled flag
    bool movieEnabled = true;
    // removeControl took it off the movie
    bool removed = false;

    explicit MCLEUICheckBox(const char *p) : path(p) {}

    void init(const std::wstring &text, int controlId, bool bChecked) {
        label = text;
        id = controlId;
        checked = bChecked;
        if (!removed) mcle_ui_call(path, "Init", {mcle_ui_str(text), mcle_ui_num(controlId), mcle_ui_bool(bChecked)});
    }
    bool IsChecked() { return checked; }
    void SetEnable(bool enable) {
        enabled = enable;
        // FJ_CheckBox::EnableCheckBox unticks a box it turns off
        if (movieEnabled != enable) {
            movieEnabled = enable;
            if (!enable) checked = false;
        }
        if (!removed) mcle_ui_call(path, "EnableCheckBox", {mcle_ui_bool(enable)});
    }
    // handleCheckboxToggled, what the movie's box is now
    void movieToggled(bool bChecked) { checked = bChecked; }
    // FJ_CheckBox's ENTER flips an enabled box, for a movie that doesn't
    // report it
    void moviePressed() {
        if (movieEnabled) checked = !checked;
    }
};

// UIControl_PlayerList
struct MCLEUIPlayerList {
    std::string path;
    int id = -1;
    int itemCount = 0;
    int currentSelection = 0;

    explicit MCLEUIPlayerList(const char *p) : path(p) {}

    void init(int controlId) {
        id = controlId;
        mcle_ui_call(path, "Init", {mcle_ui_num(controlId)});
    }
    void addItem(const std::wstring &label, int iPlayerIcon, int iVOIPIcon) {
        mcle_ui_call(path, "addNewItem", {mcle_ui_str(label), mcle_ui_num(itemCount), mcle_ui_num(iPlayerIcon + 1),
                                          mcle_ui_num(iVOIPIcon + 1)});
        ++itemCount;
    }
    void setCurrentSelection(int iSelection) { mcle_ui_call(path, "HighlightItem", {mcle_ui_num(iSelection)}); }
    int getCurrentSelection() { return currentSelection; }
    void updateChildFocus(int iChild) { currentSelection = iChild; }
    int getItemCount() { return itemCount; }
    void ReInit() {
        init(id);
        itemCount = 0;
        currentSelection = 0;
    }
};

// What the three scenes have on top of a menu scene
class InGameMenuScene : public MCLEMenuScene {
public:
    // UIScene::_handleFocusChange and _handleInitFocus, then the scene's own
    // handleFocusChange
    void handleFocusChange(int controlId, int childId, bool init) override {
        const bool changed = controlId != m_iFocusControl || childId != m_iFocusChild;
        MCLEMenuScene::handleFocusChange(controlId, childId, init);
        if (init || changed) focusChanged(controlId, childId);
    }

    void tick(double) override {}
    void handleDestroy() override {}

protected:
    virtual void focusChanged(int controlId, int childId) {}

    int getControlChildFocus() { return m_iFocusChild; }

    // UIScene::removeControl
    void removeControl(const std::string &controlName, bool centreScene) {
        mcle_ui_call("", "RemoveObject", {MCLEUIArg{MCLEUIArg::String, 0.0, controlName}, mcle_ui_bool(centreScene)});
    }
    void removeControl(MCLEUICheckBox *control, bool centreScene) {
        if (!control->removed) removeControl(control->path, centreScene);
        control->removed = true;
    }
};

void navigateToHostOptions(int iPad);
void navigateToPlayerOptions(const PlayerOptionsInitData &initData);

// UIScene_InGameInfoMenu
class InGameInfoScene : public InGameMenuScene {
    enum EControls {
        eControl_GameOptions,
        eControl_GamePlayers,
    };

    struct PlayerInfo {
        BYTE m_smallId;
        char m_voiceStatus;
        short m_colorState;
        std::wstring m_name;
    };

public:
    explicit InGameInfoScene(int iPad)
        : m_buttonGameOptions("GameOptions"), m_playerList("GamePlayers"), m_labelTitle("Title") {
        m_iPad = iPad;

        m_buttonGameOptions.init(app.GetString(IDS_HOST_OPTIONS), eControl_GameOptions);
        m_labelTitle.init(app.GetString(IDS_PLAYERS_INVITE));
        m_playerList.init(eControl_GamePlayers);

        DWORD playerCount = g_network.GetPlayerCount();

        for (DWORD i = 0; i < playerCount; ++i) {
            HostNetworkPlayer *player = g_network.GetPlayerByIndex(i);

            if (player != nullptr) {
                PlayerInfo *info = BuildPlayerInfo(player);

                m_players.push_back(info);
                m_playerList.addItem(info->m_name, info->m_colorState, info->m_voiceStatus);
            }
        }

        // RegisterPlayerChangedCallback: nobody else joins an offline game

        HostNetworkPlayer *thisPlayer = g_network.GetLocalPlayerByUserIndex(m_iPad);
        m_isHostPlayer = false;
        if (thisPlayer != nullptr) m_isHostPlayer = thisPlayer->IsHost();

        std::shared_ptr<ServerPlayer> localPlayer = mcle_local_player();
        if (!m_isHostPlayer && !localPlayer->isModerator()) {
            removeControl(m_buttonGameOptions.path, false);
        }

        updateTooltips();

        gainFocus();
    }

    ~InGameInfoScene() override {
        // Delete player infos
        for (size_t i = 0; i < m_players.size(); i++) delete m_players[i];
    }

    const char *movieName() override { return "InGameInfoMenu1080.swf"; }

    // UIScene::reloadMovie: the controls go back on the movie, handleReload,
    // the focus where it was, then gainFocus's tooltips
    void reloadMovie() override {
        m_buttonGameOptions.ReInit();
        m_playerList.ReInit();
        m_labelTitle.init(m_labelTitle.label);
        handleReload();
        mcle_ui_call("", "SetFocus", {mcle_ui_num(m_iFocusControl)});
        updateTooltips();
    }

    void handleInput(int key, bool repeat, bool pressed, bool released) override {
        mcleContainerUI.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

        switch (key) {
        case ACTION_MENU_CANCEL:
            if (pressed && !repeat) {
                mcleContainerUI.PlayUISFX(eSFX_Back);
                navigateBack();
            }
            break;
        case ACTION_MENU_Y:
            // ProfileManager.ShowProfileCard, which is empty on Windows64
            break;
        case ACTION_MENU_X:
            // SendInviteGUI, not in a local game
            break;
        case ACTION_MENU_OK:
        case ACTION_MENU_UP:
        case ACTION_MENU_DOWN:
        case ACTION_MENU_PAGEUP:
        case ACTION_MENU_PAGEDOWN:
            sendInputToMovie(key, repeat, pressed, released);
            break;
        }
    }

    void handlePress(int controlId, int childId) override {
        switch (controlId) {
        case eControl_GameOptions:
            navigateToHostOptions(m_iPad);
            break;
        case eControl_GamePlayers: {
            int currentSelection = childId;
            if (currentSelection < 0 || currentSelection >= static_cast<int>(m_players.size())) break;
            HostNetworkPlayer *selectedPlayer = g_network.GetPlayerBySmallId(m_players[currentSelection]->m_smallId);

            std::shared_ptr<ServerPlayer> localPlayer = mcle_local_player();

            bool isOp = m_isHostPlayer || localPlayer->isModerator();
            bool cheats = app.GetGameHostOption(eGameHostOption_CheatsEnabled) != 0;
            bool trust = app.GetGameHostOption(eGameHostOption_TrustPlayers) != 0;

            if (isOp && selectedPlayer != nullptr) {
                bool editingHost = selectedPlayer->IsHost();
                if ((cheats && (m_isHostPlayer || !editingHost)) || (!trust && (m_isHostPlayer || !editingHost))) {
                    PlayerOptionsInitData initData;
                    initData.iPad = m_iPad;
                    initData.networkSmallId = m_players[currentSelection]->m_smallId;
                    initData.playerPrivileges = app.GetPlayerPrivileges(m_players[currentSelection]->m_smallId);
                    navigateToPlayerOptions(initData);
                }
                // else the kick prompt, for a player on another system
            }
        } break;
        }
    }

protected:
    void updateTooltips() override {
        int keyX = IDS_TOOLTIPS_INVITE_FRIENDS;
        int ikeyY = -1;

        // XPartyGetUserList: there's no party to invite

        if (g_network.IsLocalGame()) keyX = -1;

        HostNetworkPlayer *selectedPlayer = nullptr;
        if (m_playerList.getCurrentSelection() < static_cast<int>(m_players.size())) {
            selectedPlayer = g_network.GetPlayerBySmallId(m_players[m_playerList.getCurrentSelection()]->m_smallId);
        }

        int keyA = -1;
        std::shared_ptr<ServerPlayer> localPlayer = mcle_local_player();

        bool isOp = m_isHostPlayer || localPlayer->isModerator();
        bool cheats = app.GetGameHostOption(eGameHostOption_CheatsEnabled) != 0;
        bool trust = app.GetGameHostOption(eGameHostOption_TrustPlayers) != 0;

        if (isOp) {
            if (controlHasFocus(eControl_GameOptions)) {
                keyA = IDS_TOOLTIPS_SELECT;
            } else if (selectedPlayer != nullptr) {
                bool editingHost = selectedPlayer->IsHost();
                if ((cheats && (m_isHostPlayer || !editingHost)) || (!trust && (m_isHostPlayer || !editingHost))) {
                    keyA = IDS_TOOLTIPS_PRIVILEGES;
                } else if (!selectedPlayer->IsLocal() && !selectedPlayer->IsSameSystem(g_network.GetHostPlayer())) {
                    // Only ops will hit this, can kick anyone not local and not local to the host
                    keyA = IDS_TOOLTIPS_KICK;
                }
            }
        }

        if (!controlHasFocus(eControl_GameOptions)) {
            // if the player is me, then view gamer profile
            if (selectedPlayer != nullptr && selectedPlayer->IsLocal() && selectedPlayer->GetUserIndex() == m_iPad) {
                ikeyY = IDS_TOOLTIPS_VIEW_GAMERPROFILE;
            } else {
                ikeyY = IDS_TOOLTIPS_VIEW_GAMERCARD;
            }
        }
        mcleContainerUI.SetTooltips(m_iPad, keyA, IDS_TOOLTIPS_BACK, keyX, ikeyY);
    }

    void focusChanged(int controlId, int childId) override {
        switch (controlId) {
        case eControl_GamePlayers:
            m_playerList.updateChildFocus(childId);
        }
        updateTooltips();
    }

private:
    void handleReload() {
        DWORD playerCount = g_network.GetPlayerCount();

        // Remove all player info
        for (size_t i = 0; i < m_players.size(); i++) delete m_players[i];
        m_players.clear();

        for (DWORD i = 0; i < playerCount; ++i) {
            HostNetworkPlayer *player = g_network.GetPlayerByIndex(i);

            if (player != nullptr) {
                PlayerInfo *info = BuildPlayerInfo(player);

                m_players.push_back(info);
                m_playerList.addItem(info->m_name, info->m_colorState, info->m_voiceStatus);
            }
        }

        HostNetworkPlayer *thisPlayer = g_network.GetLocalPlayerByUserIndex(m_iPad);
        m_isHostPlayer = false;
        if (thisPlayer != nullptr) m_isHostPlayer = thisPlayer->IsHost();

        std::shared_ptr<ServerPlayer> localPlayer = mcle_local_player();
        if (!m_isHostPlayer && !localPlayer->isModerator()) {
            removeControl(m_buttonGameOptions.path, false);
        }

        updateTooltips();

        if (controlHasFocus(eControl_GamePlayers)) {
            m_playerList.setCurrentSelection(getControlChildFocus());
        }
    }

    PlayerInfo *BuildPlayerInfo(HostNetworkPlayer *player) {
        PlayerInfo *info = new PlayerInfo();
        info->m_smallId = player->GetSmallId();

        std::wstring playerName = player->GetDisplayName();

        // The muted, talking and not talking icons are for players with voice
        int voiceStatus = 0;

        info->m_voiceStatus = voiceStatus;
        info->m_colorState = app.GetPlayerColour(info->m_smallId);
        info->m_name = playerName;

        return info;
    }

    bool m_isHostPlayer = false;
    std::vector<PlayerInfo *> m_players;

    MCLEUIControl m_buttonGameOptions;
    MCLEUIPlayerList m_playerList;
    MCLEUILabel m_labelTitle;
};

// UIScene_InGameHostOptionsMenu
class HostOptionsScene : public InGameMenuScene {
    enum EControls {
        eControl_FireSpreads,
        eControl_TNT,
        eControl_MobGriefing,
        eControl_KeepInventory,
        eControl_DoMobSpawning,
        eControl_DoMobLoot,
        eControl_DoTileDrops,
        eControl_NaturalRegeneration,
        eControl_DoDaylightCycle,
        eControl_TeleportToPlayer,
        eControl_TeleportToMe,
    };

public:
    explicit HostOptionsScene(int iPad)
        : m_checkboxFireSpreads("CheckboxFireSpreads"), m_checkboxTNT("CheckboxTNT"),
          m_checkboxMobGriefing("CheckboxMobGriefing"), m_checkboxKeepInventory("CheckboxKeepInventory"),
          m_checkboxDoMobSpawning("CheckboxMobSpawning"), m_checkboxDoMobLoot("CheckboxMobLoot"),
          m_checkboxDoTileDrops("CheckboxTileDrops"), m_checkboxNaturalRegeneration("CheckboxNaturalRegeneration"),
          m_checkboxDoDaylightCycle("CheckboxDayLightCycle"), m_buttonTeleportToPlayer("TeleportToPlayer"),
          m_buttonTeleportToMe("TeleportPlayerToMe") {
        m_iPad = iPad;

        m_checkboxFireSpreads.init(app.GetString(IDS_FIRE_SPREADS), eControl_FireSpreads,
                                   app.GetGameHostOption(eGameHostOption_FireSpreads) != 0);
        m_checkboxTNT.init(app.GetString(IDS_TNT_EXPLODES), eControl_TNT, app.GetGameHostOption(eGameHostOption_TNT) != 0);

        m_checkboxDoMobLoot.init(app.GetString(IDS_MOB_LOOT), eControl_DoMobLoot,
                                 app.GetGameHostOption(eGameHostOption_DoMobLoot));
        m_checkboxDoTileDrops.init(app.GetString(IDS_TILE_DROPS), eControl_DoTileDrops,
                                   app.GetGameHostOption(eGameHostOption_DoTileDrops));
        m_checkboxNaturalRegeneration.init(app.GetString(IDS_NATURAL_REGEN), eControl_NaturalRegeneration,
                                           app.GetGameHostOption(eGameHostOption_NaturalRegeneration));

        // If cheats are disabled, remove checkboxes
        if (!app.GetGameHostOption(eGameHostOption_CheatsEnabled)) {
            removeControl(&m_checkboxMobGriefing, true);
            removeControl(&m_checkboxKeepInventory, true);
            removeControl(&m_checkboxDoMobSpawning, true);
            removeControl(&m_checkboxDoDaylightCycle, true);
        }

        m_checkboxMobGriefing.init(app.GetString(IDS_MOB_GRIEFING), eControl_MobGriefing,
                                   app.GetGameHostOption(eGameHostOption_MobGriefing));
        m_checkboxKeepInventory.init(app.GetString(IDS_KEEP_INVENTORY), eControl_KeepInventory,
                                     app.GetGameHostOption(eGameHostOption_KeepInventory));
        m_checkboxDoMobSpawning.init(app.GetString(IDS_MOB_SPAWNING), eControl_DoMobSpawning,
                                     app.GetGameHostOption(eGameHostOption_DoMobSpawning));
        m_checkboxDoDaylightCycle.init(app.GetString(IDS_DAYLIGHT_CYCLE), eControl_DoDaylightCycle,
                                       app.GetGameHostOption(eGameHostOption_DoDaylightCycle));

        HostNetworkPlayer *localPlayer = g_network.GetLocalPlayerByUserIndex(m_iPad);
        unsigned int privs = app.GetPlayerPrivileges(localPlayer->GetSmallId());
        if (app.GetGameHostOption(eGameHostOption_CheatsEnabled) &&
            Player::getPlayerGamePrivilege(privs, Player::ePlayerGamePrivilege_CanTeleport) &&
            g_network.GetPlayerCount() > 1) {
            m_buttonTeleportToPlayer.init(app.GetString(IDS_TELEPORT_TO_PLAYER), eControl_TeleportToPlayer);
            m_buttonTeleportToMe.init(app.GetString(IDS_TELEPORT_TO_ME), eControl_TeleportToMe);
        } else {
            removeControl(m_buttonTeleportToPlayer.path, true);
            removeControl(m_buttonTeleportToMe.path, true);
        }

        gainFocus();
    }

    const char *movieName() override { return "InGameHostOptions1080.swf"; }

    void handleInput(int key, bool repeat, bool pressed, bool released) override {
        mcleContainerUI.AnimateKeyPress(m_iPad, key, repeat, pressed, released);
        switch (key) {
        case ACTION_MENU_CANCEL:
            if (pressed) {
                unsigned int hostOptions = app.GetGameHostOption(eGameHostOption_All);
                app.SetGameHostOption(hostOptions, eGameHostOption_FireSpreads, m_checkboxFireSpreads.IsChecked());
                app.SetGameHostOption(hostOptions, eGameHostOption_TNT, m_checkboxTNT.IsChecked());
                app.SetGameHostOption(hostOptions, eGameHostOption_DoMobLoot, m_checkboxDoMobLoot.IsChecked());
                app.SetGameHostOption(hostOptions, eGameHostOption_DoTileDrops, m_checkboxDoTileDrops.IsChecked());
                app.SetGameHostOption(hostOptions, eGameHostOption_NaturalRegeneration,
                                      m_checkboxNaturalRegeneration.IsChecked());

                // If cheats are enabled, set cheat values
                if (app.GetGameHostOption(eGameHostOption_CheatsEnabled)) {
                    app.SetGameHostOption(hostOptions, eGameHostOption_MobGriefing, m_checkboxMobGriefing.IsChecked());
                    app.SetGameHostOption(hostOptions, eGameHostOption_KeepInventory,
                                          m_checkboxKeepInventory.IsChecked());
                    app.SetGameHostOption(hostOptions, eGameHostOption_DoMobSpawning,
                                          m_checkboxDoMobSpawning.IsChecked());
                    app.SetGameHostOption(hostOptions, eGameHostOption_DoDaylightCycle,
                                          m_checkboxDoDaylightCycle.IsChecked());
                }

                // Send update settings packet to server
                if (hostOptions != app.GetGameHostOption(eGameHostOption_All)) {
                    // The server is this same game, its handler takes the packet
                    mcle_server_settings_changed(std::make_shared<ServerSettingsChangedPacket>(
                        ServerSettingsChangedPacket::HOST_IN_GAME_SETTINGS, hostOptions));
                }

                navigateBack();
            }
            break;
        case ACTION_MENU_OK:
            sendInputToMovie(key, repeat, pressed, released);
            // The focused FJ_CheckBox flips on that ENTER, and
            // InGameHostOptions1080 doesn't call handleCheckboxToggled, so
            // the box is followed here
            if (pressed && !repeat) {
                if (MCLEUICheckBox *box = checkBoxFor(m_iFocusControl)) box->moviePressed();
            }
            break;
        case ACTION_MENU_UP:
        case ACTION_MENU_DOWN:
            sendInputToMovie(key, repeat, pressed, released);
            break;
        }
    }

    // handlePress opens UIScene_TeleportMenu from the teleport buttons,
    // which this scene only has with more than one player

protected:
    void updateTooltips() override { mcleContainerUI.SetTooltips(m_iPad, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK); }

private:
    MCLEUICheckBox *checkBoxFor(int controlId) {
        MCLEUICheckBox *box = nullptr;
        switch (controlId) {
        case eControl_FireSpreads: box = &m_checkboxFireSpreads; break;
        case eControl_TNT: box = &m_checkboxTNT; break;
        case eControl_MobGriefing: box = &m_checkboxMobGriefing; break;
        case eControl_KeepInventory: box = &m_checkboxKeepInventory; break;
        case eControl_DoMobSpawning: box = &m_checkboxDoMobSpawning; break;
        case eControl_DoMobLoot: box = &m_checkboxDoMobLoot; break;
        case eControl_DoTileDrops: box = &m_checkboxDoTileDrops; break;
        case eControl_NaturalRegeneration: box = &m_checkboxNaturalRegeneration; break;
        case eControl_DoDaylightCycle: box = &m_checkboxDoDaylightCycle; break;
        }
        return (box != nullptr && !box->removed) ? box : nullptr;
    }

    MCLEUICheckBox m_checkboxFireSpreads, m_checkboxTNT, m_checkboxMobGriefing, m_checkboxKeepInventory,
        m_checkboxDoMobSpawning, m_checkboxDoMobLoot, m_checkboxDoTileDrops, m_checkboxNaturalRegeneration,
        m_checkboxDoDaylightCycle;
    MCLEUIControl m_buttonTeleportToPlayer, m_buttonTeleportToMe;
};

// PlayerConnection::handlePlayerInfo for the host's own PlayerInfoPacket,
// with a connection standing in for the game mode packet it can send
void sendHostPlayerInfo(unsigned int playerPrivileges) {
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    mcle_run_with_stand_in_connection(player, [](void *privileges) {
        mcle_tutorial_player_info(*static_cast<unsigned int *>(privileges));
    }, &playerPrivileges);
}

// UIScene_InGamePlayerOptionsMenu
class PlayerOptionsScene : public InGameMenuScene {
    enum EControls {
        // Checkboxes
        eControl_BuildAndMine,
        eControl_UseDoorsAndSwitches,
        eControl_UseContainers,
        eControl_AttackPlayers,
        eControl_AttackAnimals,
        eControl_Op,
        eControl_CheatTeleport,
        eControl_HostFly,
        eControl_HostHunger,
        eControl_HostInvisible,

        eControl_CHECKBOXES_COUNT,

        // Others
        eControl_Kick = eControl_CHECKBOXES_COUNT,
    };

public:
    explicit PlayerOptionsScene(const PlayerOptionsInitData &initData)
        : m_labelGamertag("Gamertag"),
          m_checkboxes{MCLEUICheckBox("CheckboxBuildAndMine"), MCLEUICheckBox("CheckboxUseDoorsAndSwitches"),
                       MCLEUICheckBox("CheckboxUseContainers"), MCLEUICheckBox("CheckboxAttackPlayers"),
                       MCLEUICheckBox("CheckboxAttackAnimals"), MCLEUICheckBox("CheckboxOp"),
                       MCLEUICheckBox("CheckboxTeleport"), MCLEUICheckBox("CheckboxHostFly"),
                       MCLEUICheckBox("CheckboxHostHunger"), MCLEUICheckBox("CheckboxHostInvisible")},
          m_buttonKick("ButtonKick") {
        m_iPad = initData.iPad;

        m_bShouldNavBack = false;

        m_networkSmallId = initData.networkSmallId;
        m_playerPrivileges = initData.playerPrivileges;

        HostNetworkPlayer *localPlayer = g_network.GetLocalPlayerByUserIndex(m_iPad);
        HostNetworkPlayer *editingPlayer = g_network.GetPlayerBySmallId(m_networkSmallId);

        if (editingPlayer != nullptr) {
            m_labelGamertag.init(editingPlayer->GetDisplayName());
        }

        bool trustPlayers = app.GetGameHostOption(eGameHostOption_TrustPlayers) != 0;
        bool cheats = app.GetGameHostOption(eGameHostOption_CheatsEnabled) != 0;
        m_editingSelf = (localPlayer != nullptr && localPlayer == editingPlayer);

        if (m_editingSelf || trustPlayers || (editingPlayer != nullptr && editingPlayer->IsHost())) {
            removeControl(&m_checkboxes[eControl_BuildAndMine], true);
            removeControl(&m_checkboxes[eControl_UseDoorsAndSwitches], true);
            removeControl(&m_checkboxes[eControl_UseContainers], true);
            removeControl(&m_checkboxes[eControl_AttackPlayers], true);
            removeControl(&m_checkboxes[eControl_AttackAnimals], true);
        }
        // else the trust checkboxes for another player; only the host is here

        if (m_editingSelf) {
            // Not a _DEBUG build, so no creative mode box in Op's place
            removeControl(&m_checkboxes[eControl_Op], true);

            removeControl(m_buttonKick.path, true);
            removeControl(&m_checkboxes[eControl_CheatTeleport], true);

            if (cheats) {
                bool canBeInvisible =
                    Player::getPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_CanToggleInvisible) != 0;
                m_checkboxes[eControl_HostInvisible].SetEnable(canBeInvisible);
                bool checked = canBeInvisible &&
                               (Player::getPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_Invisible) != 0 &&
                                Player::getPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_Invulnerable) != 0);
                m_checkboxes[eControl_HostInvisible].init(app.GetString(IDS_INVISIBLE), eControl_HostInvisible, checked);

                bool inCreativeMode =
                    Player::getPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_CreativeMode) != 0;
                if (inCreativeMode) {
                    removeControl(&m_checkboxes[eControl_HostFly], true);
                    removeControl(&m_checkboxes[eControl_HostHunger], true);
                } else {
                    bool canFly = Player::getPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_CanToggleFly);
                    bool canChangeHunger =
                        Player::getPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_CanToggleClassicHunger);

                    m_checkboxes[eControl_HostFly].SetEnable(canFly);
                    checked = canFly && Player::getPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_CanFly) != 0;
                    m_checkboxes[eControl_HostFly].init(app.GetString(IDS_CAN_FLY), eControl_HostFly, checked);

                    m_checkboxes[eControl_HostHunger].SetEnable(canChangeHunger);
                    checked = canChangeHunger &&
                              Player::getPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_ClassicHunger) != 0;
                    m_checkboxes[eControl_HostHunger].init(app.GetString(IDS_DISABLE_EXHAUSTION), eControl_HostHunger,
                                                           checked);
                }
            } else {
                removeControl(&m_checkboxes[eControl_HostInvisible], true);
                removeControl(&m_checkboxes[eControl_HostFly], true);
                removeControl(&m_checkboxes[eControl_HostHunger], true);
            }
        }
        // else the moderator, cheat and kick options the host has for another
        // player

        short colourIndex = app.GetPlayerColour(m_networkSmallId);
        mcle_ui_call("", "SetPlayerIcon", {mcle_ui_num(colourIndex)});

        m_bModeratorState = m_checkboxes[eControl_Op].IsChecked();

        resetCheatCheckboxes();

        // addTimer(CHECKBOXES_TIMER_ID) only follows the moderator box, and
        // RegisterPlayerChangedCallback the player leaving; editing yourself
        // in an offline game neither happens

        gainFocus();
    }

    const char *movieName() override { return "InGamePlayerOptions1080.swf"; }

    void handleInput(int key, bool repeat, bool pressed, bool released) override {
        mcleContainerUI.AnimateKeyPress(m_iPad, key, repeat, pressed, released);
        switch (key) {
        case ACTION_MENU_CANCEL:
            if (pressed) {
                bool cheats = app.GetGameHostOption(eGameHostOption_CheatsEnabled) != 0;
                if (m_editingSelf) {
                    // Not a _DEBUG build, so the creative mode privilege stays
                    if (cheats) {
                        bool canBeInvisible = Player::getPlayerGamePrivilege(
                                                  m_playerPrivileges, Player::ePlayerGamePrivilege_CanToggleInvisible) != 0;
                        if (canBeInvisible)
                            Player::setPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_Invisible,
                                                           m_checkboxes[eControl_HostInvisible].IsChecked());
                        if (canBeInvisible)
                            Player::setPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_Invulnerable,
                                                           m_checkboxes[eControl_HostInvisible].IsChecked());

                        bool inCreativeMode = Player::getPlayerGamePrivilege(
                                                  m_playerPrivileges, Player::ePlayerGamePrivilege_CreativeMode) != 0;
                        if (!inCreativeMode) {
                            bool canFly =
                                Player::getPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_CanToggleFly);
                            bool canChangeHunger = Player::getPlayerGamePrivilege(
                                m_playerPrivileges, Player::ePlayerGamePrivilege_CanToggleClassicHunger);

                            if (canFly)
                                Player::setPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_CanFly,
                                                               m_checkboxes[eControl_HostFly].IsChecked());
                            if (canChangeHunger)
                                Player::setPlayerGamePrivilege(m_playerPrivileges, Player::ePlayerGamePrivilege_ClassicHunger,
                                                               m_checkboxes[eControl_HostHunger].IsChecked());
                        }
                    }
                }
                // else what the host set for another player

                unsigned int originalPrivileges = app.GetPlayerPrivileges(m_networkSmallId);
                if (originalPrivileges != m_playerPrivileges) {
                    // Send update settings packet to server, this same game
                    sendHostPlayerInfo(m_playerPrivileges);
                }
                navigateBack();
            }
            break;
        case ACTION_MENU_OK:
        case ACTION_MENU_UP:
        case ACTION_MENU_DOWN:
            sendInputToMovie(key, repeat, pressed, released);
            break;
        }
    }

    // InGamePlayerOptions1080 calls handleCheckboxToggled with what the
    // box is now, which IsChecked reads back. Only the moderator box does
    // more, through the timer.
    void handleCheckboxToggled(int controlId, bool selected) override {
        // A box the movie hasn't had Init for yet has no id
        if (controlId < 0 || controlId >= eControl_CHECKBOXES_COUNT) return;
        MCLEUICheckBox &box = m_checkboxes[controlId];
        if (!box.removed && box.id == controlId) box.movieToggled(selected);
    }

    // handlePress is the kick button's, which isn't there editing yourself

protected:
    void updateTooltips() override { mcleContainerUI.SetTooltips(m_iPad, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK); }

private:
    // 4J-JEV: For enabling/disabling 'Can Fly', 'Can Teleport', 'Can Disable Hunger' etc
    // used after changing the moderator checkbox.
    void resetCheatCheckboxes() {
        bool isModerator = m_checkboxes[eControl_Op].IsChecked();

        if (!m_editingSelf) {
            m_checkboxes[eControl_HostInvisible].SetEnable(isModerator);
            m_checkboxes[eControl_HostFly].SetEnable(isModerator);
            m_checkboxes[eControl_HostHunger].SetEnable(isModerator);
            m_checkboxes[eControl_CheatTeleport].SetEnable(isModerator);
        }
    }

    bool m_bShouldNavBack = false;
    bool m_editingSelf = false;
    BYTE m_networkSmallId = 0;
    unsigned int m_playerPrivileges = 0;
    bool m_bModeratorState = false;

    MCLEUILabel m_labelGamertag;
    MCLEUICheckBox m_checkboxes[eControl_CHECKBOXES_COUNT];
    MCLEUIControl m_buttonKick;
};

// ui.NavigateToScene(m_iPad, eUIScene_InGameHostOptionsMenu)
void navigateToHostOptions(int iPad) {
    mcle_container_navigate_to(
        "InGameHostOptions1080.swf", [iPad]() -> MCLEContainerScene * { return new HostOptionsScene(iPad); },
        nullptr);
}

// ui.NavigateToScene(m_iPad, eUIScene_InGamePlayerOptionsMenu, pInitData)
void navigateToPlayerOptions(const PlayerOptionsInitData &initData) {
    mcle_container_navigate_to(
        "InGamePlayerOptions1080.swf",
        [initData]() -> MCLEContainerScene * { return new PlayerOptionsScene(initData); }, nullptr);
}

} // namespace

unsigned int mcle_ingame_info_buttons() {
    return InputManager.GetGameJoypadMaps(InputManager.GetJoypadMapVal(0), MINECRAFT_ACTION_GAME_INFO);
}

bool mcle_ingame_info_input_allowed() {
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    if (!mcle_local_player()) return false;
    return mcle_tutorial_input_allowed(MINECRAFT_ACTION_GAME_INFO);
}

bool mcle_ingame_info_open() {
    if (mcle_container_is_open()) return false;
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    if (!mcle_local_player()) return false;
    mcle_container_prepare();
    // NavigateToScene sets the menu displayed before it makes the scene
    mcle_game_set_menu_displayed(true);
    mcle_container_begin(new InGameInfoScene(0));
    mcleContainerUI.PlayUISFX(eSFX_Press);
    return true;
}
