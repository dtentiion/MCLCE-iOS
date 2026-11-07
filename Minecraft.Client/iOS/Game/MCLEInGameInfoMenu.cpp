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

// ui.NavigateToScene(m_iPad, eUIScene_InGameHostOptionsMenu)
void navigateToHostOptions(int iPad) {
    mcle_container_navigate_to(
        "InGameHostOptions1080.swf", [iPad]() -> MCLEContainerScene * { return new HostOptionsScene(iPad); },
        nullptr);
}

} // namespace
