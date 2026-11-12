// Compile-only stub for the upstream global `CMinecraftApp app`.
// Real CMinecraftApp lives in Minecraft.Client/Common/Consoles_App.h
// and brings in ~half the Minecraft.Client tree. For the world-probe
// every `app.X(...)` call site only needs to typecheck. Method
// bodies are no-ops returning sensible defaults.
//
// As probe coverage expands, add methods here in alphabetical order
// when the build complains about a missing one.

#pragma once

#include "iOS_WinCompat.h"

#ifdef __cplusplus

#include <string>
#include <memory>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <os/log.h>

class GAME_SETTINGS;
class C_4JProfile;
class CDLCManager;
class LevelGenerationOptions;
class CGameSettingsManager;
class FileNameMap;

extern "C" int mcle_log_msg(const char *msg);
extern "C" unsigned char mcle_settings_get(int setting); // MCLE_iOS_Settings.mm
extern "C" const wchar_t *mcle_wstring(int id);        // Game/MCLEStrings.cpp
extern "C" int mcle_html_colour(int colour);           // Game/MCLEContainerMenu.cpp
extern "C" void mcle_capture_save_thumbnail(void);     // Game/MCLESaveList.cpp
extern "C" void mcle_get_save_thumbnail(unsigned char **data, unsigned int *size);

// GAME_SETTINGS' skin fields, MCLE_iOS_Settings.mm
extern "C" unsigned int  mcle_settings_get_skin(void);
extern "C" void          mcle_settings_set_skin(unsigned int skinId);
extern "C" unsigned int  mcle_settings_get_cape(void);
extern "C" void          mcle_settings_set_cape(unsigned int capeId);
extern "C" unsigned int  mcle_settings_get_favorite_skin(int index);
extern "C" void          mcle_settings_set_favorite_skin(int index, unsigned int skinId);
extern "C" unsigned char mcle_settings_get_favorite_skins_pos(void);
extern "C" void          mcle_settings_set_favorite_skins_pos(unsigned char pos);

// The local player's setAndBroadcastCustomSkin / Cape, Game/MCLEGameLoop.cpp
void mcle_local_player_set_custom_skin(unsigned int skinId);
void mcle_local_player_set_custom_cape(unsigned int capeId);
// Player::getCapeIdFromPath / getCapePathFromId, Game/MCLESkinSelectMenu.cpp
unsigned int mcle_cape_id_from_path(const std::wstring &cape);
std::wstring mcle_cape_path_from_id(unsigned int capeId);

// App_structs.h's FEATURE_DATA, one place in the terrain feature list
typedef struct {
    int x, z;
    _eTerrainFeatureType eTerrainFeature;
} FEATURE_DATA;

struct McleAppStub {
    // Heavyweight returning ptr to opaque object; nullptr is fine
    // for most call sites that just check for non-null OR pass it
    // forward.
    template<class T> T* getOpaque() { return nullptr; }

    // Direct fields upstream uses without parens.
    // Real Consoles_App.h:79 has `DLCManager m_dlcManager;` (by value).
    // Player.cpp does `app.m_dlcManager.getSkinFile(...)` so it needs the
    // real DLCManager type, complete. iOS_stdafx.h pre-includes
    // DLCFile.h -> DLCManager.h before this header so the type is
    // visible.
    DLCManager m_dlcManager;
    // Real `std::vector<wstring> vSkinNames` from Consoles_App.h.
    std::vector<std::wstring> vSkinNames;
    // Consoles_App.h's game rules, and what CMinecraftApp passes through to them
    GameRuleManager m_gameRules;
    void processSchematics(LevelChunk *levelChunk) { m_gameRules.processSchematics(levelChunk); }
    void processSchematicsLighting(LevelChunk *levelChunk) { m_gameRules.processSchematicsLighting(levelChunk); }
    void loadDefaultGameRules() { m_gameRules.loadDefaultGameRules(); }
    std::vector<LevelGenerationOptions *> *getLevelGenerators() { return m_gameRules.getLevelGenerators(); }
    void setLevelGenerationOptions(LevelGenerationOptions *levelGen) { m_gameRules.setLevelGenerationOptions(levelGen); }
    LevelRuleset *getGameRuleDefinitions() { return m_gameRules.getGameRuleDefinitions(); }
    LevelGenerationOptions *getLevelGenerationOptions() { return m_gameRules.getLevelGenerationOptions(); }
    LPCWSTR GetGameRulesString(const std::wstring &key) { return m_gameRules.GetGameRulesString(key); }

    // CMinecraftApp's terrain feature list (Consoles_App.cpp): where level
    // generation put the structures, which loadLevel takes the stronghold from
    std::vector<FEATURE_DATA *> m_vTerrainFeatures;
    void AddTerrainFeaturePosition(_eTerrainFeatureType eFeatureType, int x, int z) {
        // check we don't already have this in
        for (FEATURE_DATA *pFeatureData : m_vTerrainFeatures) {
            if ((pFeatureData->eTerrainFeature == eFeatureType) && (pFeatureData->x == x) && (pFeatureData->z == z)) return;
        }
        FEATURE_DATA *pFeatureData = new FEATURE_DATA;
        pFeatureData->eTerrainFeature = eFeatureType;
        pFeatureData->x = x;
        pFeatureData->z = z;
        m_vTerrainFeatures.push_back(pFeatureData);
    }
    _eTerrainFeatureType IsTerrainFeature(int x, int z) {
        for (FEATURE_DATA *pFeatureData : m_vTerrainFeatures) {
            if ((pFeatureData->x == x) && (pFeatureData->z == z)) return pFeatureData->eTerrainFeature;
        }
        return eTerrainFeature_None;
    }
    bool GetTerrainFeaturePosition(_eTerrainFeatureType eType, int *pX, int *pZ) {
        for (const FEATURE_DATA *pFeatureData : m_vTerrainFeatures) {
            if (pFeatureData->eTerrainFeature == eType) {
                *pX = pFeatureData->x;
                *pZ = pFeatureData->z;
                return true;
            }
        }
        return false;
    }
    void ClearTerrainFeaturePosition() {
        while (m_vTerrainFeatures.size() > 0) {
            FEATURE_DATA *pFeatureData = m_vTerrainFeatures.back();
            m_vTerrainFeatures.pop_back();
            delete pFeatureData;
        }
    }

    bool m_bTutorialMode = false;
    bool GetTutorialMode() { return m_bTutorialMode; }
    void SetTutorialMode(bool bSet) { m_bTutorialMode = bSet; }

    // Generic templates - accept any args, return either a default-
    // constructible value or void. Compile-only, not link-only.
    // CMinecraftApp::GetString, the languages.loc string table
    template<class T> const wchar_t* GetString(T id)            { return mcle_wstring(static_cast<int>(id)); }
    // Real CMinecraftApp::DebugPrintf is a printf wrapper. Route it
    // through mcle_log_msg (NSLog) so the formatted string isn't
    // redacted as <private> by os_log on sideloaded apps without
    // entitlements - %{public}s is ignored without them.
    int DebugPrintf(const char *fmt, ...) {
        char buf[1024];
        va_list ap; va_start(ap, fmt);
        int n = ::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n < 0) return n;
        if (n > 0 && buf[n-1] == '\n') buf[n-1] = 0;
        mcle_log_msg(buf);
        return n;
    }
    int DebugPrintf(const wchar_t *, ...) { return 0; }
    // The per-user variant only prints for the developer whose id matches
    int DebugPrintf(int, const char *, ...) { return 0; }
    template<class... A> void           FatalLoadError(A...)    {}
    template<class... A> int            GetGameSetting(A...)    { return 0; }
    // Real upstream has two overloads:
    //   GAME_SETTINGS* GetGameSettings(int pad)
    //   int            GetGameSettings(int pad, int settingId)   <-- Input.cpp uses this
    // The stub provides both so static_cast<float>(GetGameSettings(p,s))
    // resolves to int->float instead of pointer->float.
    GAME_SETTINGS *GetGameSettings(int /*pad*/)               { return nullptr; }
    // The iOS settings store (MCLE_iOS_Settings.mm) keeps the eGameSetting
    // order, so the player's settings reach the upstream code that reads
    // them (clouds, Display HUD / Hand, skin animation)
    int            GetGameSettings(int /*pad*/, int setting) {
        if (setting >= 0 && setting < 32) return mcle_settings_get(setting);
        return 0;
    }
    // The primary pad's, CMinecraftApp::GetGameSettings(eGameSetting)
    unsigned char  GetGameSettings(eGameSetting eVal) {
        return static_cast<unsigned char>(GetGameSettings(0, static_cast<int>(eVal)));
    }
    template<class... A> int            GetGameSettingsDebugMask(A...) { return 0; }
    // Host options, as in CMinecraftApp (Consoles_App.cpp). Difficulty, game
    // type and the game rules (mob spawning, tile drops, regen, fire...)
    // all read through these. LoadMenu sets them before a world starts.
    unsigned int m_uiGameHostSettings = 0;

    unsigned int GetGameHostOption(eGameHostOption eVal) {
        return GetGameHostOption(m_uiGameHostSettings, eVal);
    }

    unsigned int GetGameHostOption(unsigned int uiHostSettings, eGameHostOption eVal) {
        switch (eVal) {
        case eGameHostOption_FriendsOfFriends: return (uiHostSettings & GAME_HOST_OPTION_BITMASK_FRIENDSOFFRIENDS);
        case eGameHostOption_Difficulty:       return (uiHostSettings & GAME_HOST_OPTION_BITMASK_DIFFICULTY);
        case eGameHostOption_Gamertags:        return (uiHostSettings & GAME_HOST_OPTION_BITMASK_GAMERTAGS);
        case eGameHostOption_GameType:         return (uiHostSettings & GAME_HOST_OPTION_BITMASK_GAMETYPE) >> 4;
        case eGameHostOption_All:              return (uiHostSettings & GAME_HOST_OPTION_BITMASK_ALL);
        case eGameHostOption_Tutorial:
            // tutorial is offline, but keeps the gamertag option, Easy, structures, fire, tnt, pvp, trust
            return ((uiHostSettings & GAME_HOST_OPTION_BITMASK_GAMERTAGS) |
                    GAME_HOST_OPTION_BITMASK_TRUSTPLAYERS | GAME_HOST_OPTION_BITMASK_FIRESPREADS |
                    GAME_HOST_OPTION_BITMASK_TNT | GAME_HOST_OPTION_BITMASK_PVP |
                    GAME_HOST_OPTION_BITMASK_STRUCTURES | 1);
        case eGameHostOption_LevelType:          return (uiHostSettings & GAME_HOST_OPTION_BITMASK_LEVELTYPE);
        case eGameHostOption_Structures:         return (uiHostSettings & GAME_HOST_OPTION_BITMASK_STRUCTURES);
        case eGameHostOption_BonusChest:         return (uiHostSettings & GAME_HOST_OPTION_BITMASK_BONUSCHEST);
        case eGameHostOption_HasBeenInCreative:  return (uiHostSettings & GAME_HOST_OPTION_BITMASK_BEENINCREATIVE);
        case eGameHostOption_PvP:                return (uiHostSettings & GAME_HOST_OPTION_BITMASK_PVP);
        case eGameHostOption_TrustPlayers:       return (uiHostSettings & GAME_HOST_OPTION_BITMASK_TRUSTPLAYERS);
        case eGameHostOption_TNT:                return (uiHostSettings & GAME_HOST_OPTION_BITMASK_TNT);
        case eGameHostOption_FireSpreads:        return (uiHostSettings & GAME_HOST_OPTION_BITMASK_FIRESPREADS);
        case eGameHostOption_CheatsEnabled:
            return (uiHostSettings & (GAME_HOST_OPTION_BITMASK_HOSTFLY | GAME_HOST_OPTION_BITMASK_HOSTHUNGER |
                                      GAME_HOST_OPTION_BITMASK_HOSTINVISIBLE));
        case eGameHostOption_HostCanFly:          return (uiHostSettings & GAME_HOST_OPTION_BITMASK_HOSTFLY);
        case eGameHostOption_HostCanChangeHunger: return (uiHostSettings & GAME_HOST_OPTION_BITMASK_HOSTHUNGER);
        case eGameHostOption_HostCanBeInvisible:  return (uiHostSettings & GAME_HOST_OPTION_BITMASK_HOSTINVISIBLE);
        case eGameHostOption_BedrockFog:          return (uiHostSettings & GAME_HOST_OPTION_BITMASK_BEDROCKFOG);
        case eGameHostOption_DisableSaving:       return (uiHostSettings & GAME_HOST_OPTION_BITMASK_DISABLESAVE);
        case eGameHostOption_WasntSaveOwner:      return (uiHostSettings & GAME_HOST_OPTION_BITMASK_NOTOWNER);
        case eGameHostOption_WorldSize:
            return (uiHostSettings & GAME_HOST_OPTION_BITMASK_WORLDSIZE) >> GAME_HOST_OPTION_BITMASK_WORLDSIZE_BITSHIFT;
        case eGameHostOption_MobGriefing:         return !(uiHostSettings & GAME_HOST_OPTION_BITMASK_MOBGRIEFING);
        case eGameHostOption_KeepInventory:       return (uiHostSettings & GAME_HOST_OPTION_BITMASK_KEEPINVENTORY);
        case eGameHostOption_DoMobSpawning:       return !(uiHostSettings & GAME_HOST_OPTION_BITMASK_DOMOBSPAWNING);
        case eGameHostOption_DoMobLoot:           return !(uiHostSettings & GAME_HOST_OPTION_BITMASK_DOMOBLOOT);
        case eGameHostOption_DoTileDrops:         return !(uiHostSettings & GAME_HOST_OPTION_BITMASK_DOTILEDROPS);
        case eGameHostOption_NaturalRegeneration: return !(uiHostSettings & GAME_HOST_OPTION_BITMASK_NATURALREGEN);
        case eGameHostOption_DoDaylightCycle:     return !(uiHostSettings & GAME_HOST_OPTION_BITMASK_DODAYLIGHTCYCLE);
        default: break;
        }
        return false;
    }

    void SetGameHostOption(eGameHostOption eVal, unsigned int uiVal) {
        SetGameHostOption(m_uiGameHostSettings, eVal, uiVal);
    }

    void SetGameHostOption(unsigned int &uiHostSettings, eGameHostOption eVal, unsigned int uiVal) {
        // Plain on/off bits
        auto setBit = [&](unsigned int mask, bool on) {
            if (on) uiHostSettings |= mask;
            else uiHostSettings &= ~mask;
        };
        switch (eVal) {
        case eGameHostOption_FriendsOfFriends:  setBit(GAME_HOST_OPTION_BITMASK_FRIENDSOFFRIENDS, uiVal != 0); break;
        case eGameHostOption_Difficulty:
            uiHostSettings &= ~GAME_HOST_OPTION_BITMASK_DIFFICULTY;
            uiHostSettings |= (GAME_HOST_OPTION_BITMASK_DIFFICULTY & uiVal);
            break;
        case eGameHostOption_Gamertags:         setBit(GAME_HOST_OPTION_BITMASK_GAMERTAGS, uiVal != 0); break;
        case eGameHostOption_GameType:
            uiHostSettings &= ~GAME_HOST_OPTION_BITMASK_GAMETYPE;
            uiHostSettings |= (GAME_HOST_OPTION_BITMASK_GAMETYPE & (uiVal << 4));
            break;
        case eGameHostOption_LevelType:         setBit(GAME_HOST_OPTION_BITMASK_LEVELTYPE, uiVal != 0); break;
        case eGameHostOption_Structures:        setBit(GAME_HOST_OPTION_BITMASK_STRUCTURES, uiVal != 0); break;
        case eGameHostOption_BonusChest:        setBit(GAME_HOST_OPTION_BITMASK_BONUSCHEST, uiVal != 0); break;
        case eGameHostOption_HasBeenInCreative: setBit(GAME_HOST_OPTION_BITMASK_BEENINCREATIVE, uiVal != 0); break;
        case eGameHostOption_PvP:               setBit(GAME_HOST_OPTION_BITMASK_PVP, uiVal != 0); break;
        case eGameHostOption_TrustPlayers:      setBit(GAME_HOST_OPTION_BITMASK_TRUSTPLAYERS, uiVal != 0); break;
        case eGameHostOption_TNT:               setBit(GAME_HOST_OPTION_BITMASK_TNT, uiVal != 0); break;
        case eGameHostOption_FireSpreads:       setBit(GAME_HOST_OPTION_BITMASK_FIRESPREADS, uiVal != 0); break;
        case eGameHostOption_CheatsEnabled:
            setBit(GAME_HOST_OPTION_BITMASK_HOSTFLY, uiVal != 0);
            setBit(GAME_HOST_OPTION_BITMASK_HOSTHUNGER, uiVal != 0);
            setBit(GAME_HOST_OPTION_BITMASK_HOSTINVISIBLE, uiVal != 0);
            break;
        case eGameHostOption_HostCanFly:          setBit(GAME_HOST_OPTION_BITMASK_HOSTFLY, uiVal != 0); break;
        case eGameHostOption_HostCanChangeHunger: setBit(GAME_HOST_OPTION_BITMASK_HOSTHUNGER, uiVal != 0); break;
        case eGameHostOption_HostCanBeInvisible:  setBit(GAME_HOST_OPTION_BITMASK_HOSTINVISIBLE, uiVal != 0); break;
        case eGameHostOption_BedrockFog:          setBit(GAME_HOST_OPTION_BITMASK_BEDROCKFOG, uiVal != 0); break;
        case eGameHostOption_DisableSaving:       setBit(GAME_HOST_OPTION_BITMASK_DISABLESAVE, uiVal != 0); break;
        case eGameHostOption_WasntSaveOwner:      setBit(GAME_HOST_OPTION_BITMASK_NOTOWNER, uiVal != 0); break;
        // These are stored inverted, a set bit means off
        case eGameHostOption_MobGriefing:         setBit(GAME_HOST_OPTION_BITMASK_MOBGRIEFING, uiVal != 1); break;
        case eGameHostOption_KeepInventory:       setBit(GAME_HOST_OPTION_BITMASK_KEEPINVENTORY, uiVal != 0); break;
        case eGameHostOption_DoMobSpawning:       setBit(GAME_HOST_OPTION_BITMASK_DOMOBSPAWNING, uiVal != 1); break;
        case eGameHostOption_DoMobLoot:           setBit(GAME_HOST_OPTION_BITMASK_DOMOBLOOT, uiVal != 1); break;
        case eGameHostOption_DoTileDrops:         setBit(GAME_HOST_OPTION_BITMASK_DOTILEDROPS, uiVal != 1); break;
        case eGameHostOption_NaturalRegeneration: setBit(GAME_HOST_OPTION_BITMASK_NATURALREGEN, uiVal != 1); break;
        case eGameHostOption_DoDaylightCycle:     setBit(GAME_HOST_OPTION_BITMASK_DODAYLIGHTCYCLE, uiVal != 1); break;
        case eGameHostOption_WorldSize:
            uiHostSettings &= ~GAME_HOST_OPTION_BITMASK_WORLDSIZE;
            uiHostSettings |= (GAME_HOST_OPTION_BITMASK_WORLDSIZE & (uiVal << GAME_HOST_OPTION_BITMASK_WORLDSIZE_BITSHIFT));
            break;
        case eGameHostOption_All:
            uiHostSettings = uiVal;
            break;
        default: break;
        }
    }
    // CMinecraftApp::UpdatePlayerInfo, GetPlayerColour and GetPlayerPrivileges:
    // what the client's PlayerInfoPackets said about each player, by colour
    // index (the QNet small id there and its privileges)
    BYTE m_playerColours[MINECRAFT_NET_MAX_PLAYERS] = {};
    unsigned int m_playerGamePrivileges[MINECRAFT_NET_MAX_PLAYERS] = {};

    void UpdatePlayerInfo(BYTE networkSmallId, SHORT playerColourIndex, unsigned int playerGamePrivileges) {
        for (unsigned int i = 0; i < MINECRAFT_NET_MAX_PLAYERS; ++i) {
            if (m_playerColours[i] == networkSmallId) {
                m_playerColours[i] = 0;
                m_playerGamePrivileges[i] = 0;
            }
        }
        if (playerColourIndex >= 0 && playerColourIndex < MINECRAFT_NET_MAX_PLAYERS) {
            m_playerColours[playerColourIndex] = networkSmallId;
            m_playerGamePrivileges[playerColourIndex] = playerGamePrivileges;
        }
    }

    short GetPlayerColour(BYTE networkSmallId) {
        short index = -1;
        for (unsigned int i = 0; i < MINECRAFT_NET_MAX_PLAYERS; ++i) {
            if (m_playerColours[i] == networkSmallId) {
                index = i;
                break;
            }
        }
        return index;
    }

    unsigned int GetPlayerPrivileges(BYTE networkSmallId) {
        unsigned int privileges = 0;
        for (unsigned int i = 0; i < MINECRAFT_NET_MAX_PLAYERS; ++i) {
            if (m_playerColours[i] == networkSmallId) {
                privileges = m_playerGamePrivileges[i];
                break;
            }
        }
        return privileges;
    }

    template<class... A> int            GetGameNewHellScale(A...) { return 1; }
    template<class... A> int            GetGameNewWorldSize(A...) { return 0; }
    template<class... A> bool           GetGameNewWorldSizeUseMoat(A...) { return false; }
    // CMinecraftApp::GetHTMLColour, the texture pack's colour table
    template<class T> int               GetHTMLColour(T colour) { return mcle_html_colour(static_cast<int>(colour)); }
    template<class T> int               GetHTMLColor(T colour)  { return GetHTMLColour(colour); }
    template<class... A> bool           GetMobsDontAttackEnabled(A...) { return false; }
    template<class... A> bool           GetMobsDontTickEnabled(A...)   { return false; }
    template<class... A> bool           GetWriteSavesToFolderEnabled(A...) { return false; }
    // LoadMenu's More Options can ask for the Nether to be made again
    bool m_bResetNether = false;
    void SetResetNether(bool bResetNether) { m_bResetNether = bResetNether; }
    bool GetResetNether() { return m_bResetNether; }
    template<class... A> bool           DebugArtToolsOn(A...)   { return false; }
    template<class... A> bool           DebugSettingsOn(A...)   { return false; }
    template<class... A> bool           DefaultCapeExists(A...) { return false; }
    template<class... A> bool           isXuidNotch(A...)       { return false; }
    template<class... A> int64_t        getAppTime(A...)        { return 0; }

    // CMinecraftApp's player skin and cape, kept in the settings store like
    // GAME_SETTINGS keeps them
    void SetPlayerSkin(int iPad, const std::wstring &name) {
        DWORD skinId = getSkinIdFromPath(name);

        SetPlayerSkin(iPad, skinId);
    }

    void SetPlayerSkin(int iPad, DWORD dwSkinId) {
        DebugPrintf("Setting skin for %d to %08X\n", iPad, dwSkinId);

        mcle_settings_set_skin(dwSkinId);

        mcle_local_player_set_custom_skin(dwSkinId);
    }

    std::wstring GetPlayerSkinName(int iPad) {
        return getSkinPathFromId(mcle_settings_get_skin());
    }

    DWORD GetPlayerSkinId(int iPad) {
        // 4J-PB -check the user has rights to use this skin - they may have had at some point but the entitlement has been removed.
        DLCPack *Pack = nullptr;
        DLCSkinFile *skinFile = nullptr;
        DWORD dwSkin = mcle_settings_get_skin();
        wchar_t chars[256];

        if (GET_IS_DLC_SKIN_FROM_BITMASK(dwSkin)) {
            // 4J Stu - DLC skins are numbered using decimal rather than hex to make it easier to number manually
            swprintf(chars, 256, L"dlcskin%08d.png", GET_DLC_SKIN_ID_FROM_BITMASK(dwSkin));

            Pack = m_dlcManager.getPackContainingSkin(chars);

            if (Pack) {
                skinFile = Pack->getSkinFile(chars);

                bool bSkinIsFree = skinFile->getParameterAsBool(DLCManager::e_DLCParamType_Free);
                bool bLicensed = Pack->hasPurchasedFile(DLCManager::e_DLCType_Skin, skinFile->getPath());

                if (bSkinIsFree || bLicensed) {
                    return dwSkin;
                } else {
                    return 0;
                }
            }
        }

        return dwSkin;
    }

    void SetPlayerCape(int iPad, const std::wstring &name) {
        DWORD capeId = mcle_cape_id_from_path(name);

        SetPlayerCape(iPad, capeId);
    }

    void SetPlayerCape(int iPad, DWORD dwCapeId) {
        DebugPrintf("Setting cape for %d to %08X\n", iPad, dwCapeId);

        mcle_settings_set_cape(dwCapeId);

        mcle_local_player_set_custom_cape(dwCapeId);
    }

    std::wstring GetPlayerCapeName(int iPad) { return mcle_cape_path_from_id(mcle_settings_get_cape()); }

    DWORD GetPlayerCapeId(int iPad) { return mcle_settings_get_cape(); }

    void SetPlayerFavoriteSkin(int iPad, int iIndex, unsigned int uiSkinID) {
        DebugPrintf("Setting favorite skin for %d to %08X\n", iPad, uiSkinID);

        mcle_settings_set_favorite_skin(iIndex, uiSkinID);
    }

    unsigned int GetPlayerFavoriteSkin(int iPad, int iIndex) { return mcle_settings_get_favorite_skin(iIndex); }

    unsigned char GetPlayerFavoriteSkinsPos(int iPad) { return mcle_settings_get_favorite_skins_pos(); }

    void SetPlayerFavoriteSkinsPos(int iPad, int iPos) {
        mcle_settings_set_favorite_skins_pos(static_cast<unsigned char>(iPos));
    }

    unsigned int GetPlayerFavoriteSkinsCount(int iPad) {
        unsigned int uiCount = 0;
        for (int i = 0; i < MAX_FAVORITE_SKINS; i++) {
            if (mcle_settings_get_favorite_skin(i) != 0xFFFFFFFF) {
                uiCount++;
            } else {
                break;
            }
        }
        return uiCount;
    }

    void ValidateFavoriteSkins(int iPad) {
        unsigned int uiCount = GetPlayerFavoriteSkinsCount(iPad);

        // remove invalid skins
        unsigned int uiValidSkin = 0;
        wchar_t chars[256];

        for (unsigned int i = 0; i < uiCount; i++) {
            // get the pack number from the skin id
            swprintf(chars, 256, L"dlcskin%08d.png", GetPlayerFavoriteSkin(iPad, i));

            // Also check they haven't reverted to a trial pack
            DLCPack *pDLCPack = m_dlcManager.getPackContainingSkin(chars);

            if (pDLCPack != nullptr) {
                // 4J-PB - We should let players add the free skins to their favourites as well!
                DLCSkinFile *pSkinFile = pDLCPack->getSkinFile(chars);

                if (pDLCPack->hasPurchasedFile(DLCManager::e_DLCType_Skin, L"") || (pSkinFile && pSkinFile->isFree())) {
                    mcle_settings_set_favorite_skin(uiValidSkin++, mcle_settings_get_favorite_skin(i));
                }
            }
        }

        for (unsigned int i = uiValidSkin; i < MAX_FAVORITE_SKINS; i++) {
            mcle_settings_set_favorite_skin(i, 0xFFFFFFFF);
        }
    }

    // CMinecraftApp's DLC install: the installed packs are mounted one at a
    // time and their .pck files read into m_dlcManager (Game/MCLEDLC.cpp)
    bool m_bDLCInstallProcessCompleted = false;
    bool m_bDLCInstallPending = false;
    int m_iTotalDLC = 0;
    int m_iTotalDLCInstalled = 0;

    bool DLCInstallProcessCompleted() { return m_bDLCInstallProcessCompleted; }
    bool DLCInstallPending() { return m_bDLCInstallPending; }
    bool StartInstallDLCProcess(int iPad);
    static int DLCInstalledCallback(LPVOID pParam, int iInstalledC, int iPad);
    void MountNextDLC(int iPad);
    static int DLCMountedCallback(LPVOID pParam, int iPad, DWORD dwErr, DWORD dwLicenceMask);
    void HandleDLC(DLCPack *pack);

    // CMinecraftApp's memory texture files: DLC skins and capes, by name
    void AddMemoryTextureFile(const std::wstring &wName, PBYTE pbData, DWORD dwBytes);
    void RemoveMemoryTextureFile(const std::wstring &wName);
    bool IsFileInMemoryTextures(const std::wstring &wName);
    void GetMemFileDetails(const std::wstring &wName, PBYTE *ppbData, DWORD *pdwBytes);

    // CMinecraftApp's texture pack side (Game/MCLETexturePacks.cpp): the pack
    // a game asked for, the app actions (the texture pack reload is the one
    // with a handler on iOS), the files on a mounted pack, and the media
    // archive with the selected pack's archive in front of it
    DWORD m_dwRequiredTexturePackID = 0;
    DWORD GetRequiredTexturePackID() { return m_dwRequiredTexturePackID; }
    void SetRequiredTexturePackID(DWORD dwID) { m_dwRequiredTexturePackID = dwID; }
    eXuiAction m_eXuiAction[XUSER_MAX_COUNT] = {};
    LPVOID m_eXuiActionParam[XUSER_MAX_COUNT] = {};
    void SetAction(int iPad, eXuiAction action, LPVOID param = nullptr);
    eXuiAction GetXuiAction(int iPad) { return m_eXuiAction[iPad]; }
    void HandleXuiActions();
    std::wstring getFilePath(DWORD packId, std::wstring filename, bool bAddDataFolder,
                             std::wstring mountPoint = L"TPACK:");
    std::wstring getRootPath(DWORD packId, bool allowOverride, bool bAddDataFolder, std::wstring mountPoint);
    int getArchiveFileSize(const std::wstring &filename);
    bool hasArchiveFile(const std::wstring &filename);
    byteArray getArchiveFile(const std::wstring &filename);

    DWORD getSkinIdFromPath(const std::wstring &skin) {
        bool dlcSkin = false;
        unsigned int skinId = 0;

        if (skin.size() >= 14) {
            dlcSkin = skin.substr(0, 3).compare(L"dlc") == 0;

            std::wstring skinValue = skin.substr(7, skin.size());
            skinValue = skinValue.substr(0, skinValue.find_first_of(L'.'));

            std::wstringstream ss;
            // 4J Stu - dlc skins are numbered using decimal to make it easier for artists/people to number manually
            // Everything else is numbered using hex
            if (dlcSkin)
                ss << std::dec << skinValue.c_str();
            else
                ss << std::hex << skinValue.c_str();
            ss >> skinId;

            skinId = MAKE_SKIN_BITMASK(dlcSkin, skinId);
        }
        return skinId;
    }

    std::wstring getSkinPathFromId(DWORD skinId) {
        // 4J Stu - This function maps the encoded DWORD we store in the player profile
        // to a filename that is stored as a memory texture and shared between systems in game
        wchar_t chars[256];
        if (GET_IS_DLC_SKIN_FROM_BITMASK(skinId)) {
            // 4J Stu - DLC skins are numbered using decimal rather than hex to make it easier to number manually
            swprintf(chars, 256, L"dlcskin%08d.png", GET_DLC_SKIN_ID_FROM_BITMASK(skinId));
        } else {
            DWORD ugcSkinIndex = GET_UGC_SKIN_ID_FROM_BITMASK(skinId);
            DWORD defaultSkinIndex = GET_DEFAULT_SKIN_ID_FROM_BITMASK(skinId);
            if (ugcSkinIndex == 0) {
                swprintf(chars, 256, L"defskin%08X.png", defaultSkinIndex);
            } else {
                swprintf(chars, 256, L"ugcskin%08X.png", ugcSkinIndex);
            }
        }
        return chars;
    }
    // CConsoleMinecraftApp::CaptureSaveThumbnail / GetSaveThumbnail
    // (Windows64_App.cpp), the world frame taken for the save (MCLESaveList.cpp)
    void CaptureSaveThumbnail() { mcle_capture_save_thumbnail(); }
    void GetSaveThumbnail(PBYTE *pbData, DWORD *pdwSize) {
        unsigned int size = 0;
        mcle_get_save_thumbnail(pbData, pdwSize ? &size : nullptr);
        if (pdwSize) *pdwSize = size;
    }
    // Real signature returns `MOJANG_DATA*` - declared in stdafx.
    template<class... A> struct MOJANG_DATA* GetMojangDataForXuid(A...) { return nullptr; }
    // A DLC skin's extra boxes as parts on the player model, and its
    // animation overrides (Game/MCLEDLC.cpp)
    void SetAdditionalSkinBoxes(DWORD dwSkinID, SKIN_BOX *SkinBoxA, DWORD dwSkinBoxC);
    std::vector<class ModelPart*>* SetAdditionalSkinBoxes(DWORD dwSkinID, std::vector<SKIN_BOX *> *pvSkinBoxA);
    std::vector<class ModelPart*>* GetAdditionalModelParts(DWORD dwSkinID);
    std::vector<SKIN_BOX *> *GetAdditionalSkinBoxes(DWORD dwSkinID);
    unsigned int GetAnimOverrideBitmask(DWORD dwSkinID);
    void SetAnimOverrideBitmask(DWORD dwSkinID, unsigned int uiAnimOverrideBitmask);
    template<class... A> void           SetUniqueMapName(A...)        {}
    template<class... A> void           SetXuiServerAction(A...)      {}
    // Returns the eXuiServerAction enum so MinecraftServer.cpp's
    // `eAction = app.GetXuiServerAction(i);` typechecks. Default value
    // is the 0-th enumerator (eXuiServerAction_None or equivalent).
    template<class... A> eXuiServerAction GetXuiServerAction(A...)     { return (eXuiServerAction)0; }
    template<class... A> void*          GetXuiServerActionParam(A...) { return nullptr; }
    template<class... A> void           SetXuiServerActionParam(A...) {}
    template<class... A> void           EnterSaveNotificationSection(A...) {}
    template<class... A> void           LeaveSaveNotificationSection(A...) {}
    template<class... A> bool           IsSaveNotificationSection(A...) { return false; }
    template<class... A> void           SetDisconnectReason(A...)     {}
    template<class... A> int            GetDisconnectReason(A...)     { return 0; }
    template<class... A> void           SetGameRules(A...)            {}
    template<class... A> void           ClearGameRules(A...)          {}
    template<class... A> void           SetLevelGenerationOptions(A...) {}
    template<class... A> void           SetMapName(A...)              {}
    template<class... A> void           SetSeed(A...)                 {}
    template<class... A> bool           GetGameStarted(A...)          { return false; }
    template<class... A> void           SetGameStarted(A...)          {}
    template<class... A> bool           GetIsLoading(A...)            { return false; }
    template<class... A> void           SetIsLoading(A...)            {}
    template<class... A> bool           GetIsSaving(A...)             { return false; }
    template<class... A> bool           GetIsConnecting(A...)         { return false; }
    template<class... A> bool           GetIsHosting(A...)            { return false; }
    template<class... A> int            GetGameTime(A...)             { return 0; }
    template<class... A> int            GetTickCount(A...)            { return 0; }
    template<class... A> bool           IsTutorialActive(A...)        { return false; }
    template<class... A> bool           IsTrialMode(A...)             { return false; }
    template<class... A> bool           IsDemoMode(A...)              { return false; }
    // CMinecraftApp::CreateImageTextData: the save's seed, host options and
    // texture pack as text for its thumbnail png, which LoadMenu reads back
    unsigned int CreateImageTextData(PBYTE bTextMetadata, int64_t seed, bool hasSeed, unsigned int uiHostOptions,
                                     unsigned int uiTexturePackId) {
        int iTextMetadataBytes = 0;
        if (hasSeed) {
            strcpy((char *)bTextMetadata, "4J_SEED");
            snprintf((char *)&bTextMetadata[8], 42, "%lld", (long long)seed);

            // get the length
            iTextMetadataBytes += 8;
            while (bTextMetadata[iTextMetadataBytes] != 0) iTextMetadataBytes++;
            ++iTextMetadataBytes; // Add a null terminator at the end of the seed value
        }

        // Save the host options that this world was last played with
        strcpy((char *)&bTextMetadata[iTextMetadataBytes], "4J_HOSTOPTIONS");
        snprintf((char *)&bTextMetadata[iTextMetadataBytes + 15], 9, "%x", uiHostOptions);

        iTextMetadataBytes += 15;
        while (bTextMetadata[iTextMetadataBytes] != 0) iTextMetadataBytes++;
        ++iTextMetadataBytes; // Add a null terminator at the end of the host options value

        // Save the texture pack id
        strcpy((char *)&bTextMetadata[iTextMetadataBytes], "4J_TEXTUREPACK");
        snprintf((char *)&bTextMetadata[iTextMetadataBytes + 15], 9, "%x", uiTexturePackId);

        iTextMetadataBytes += 15;
        while (bTextMetadata[iTextMetadataBytes] != 0) iTextMetadataBytes++;

        return iTextMetadataBytes;
    }
    // English only for now: the tail of CMinecraftApp::getLocale, which
    // always ends with en-US then en-EN.
    void getLocale(std::vector<std::wstring> &out) { out.push_back(L"en-US"); out.push_back(L"en-EN"); }
    template<class... A> bool           isXuidDeadmau5(A...)          { return false; }
    template<class... A> int            GetLocalPlayerCount(A...)     { return 1; }
    template<class... A> bool           LoadSignEntryMenu(A...)       { return false; }

    // Common/* additions: catch-all setters / getters / state
    // probes that upstream code in Minecraft.Client/Common/* uses.
    // Bool-returning style for any "Is*" / "Has*" / "Get*Enabled".
    template<class... A> bool           IsXX(A...)                    { return false; }
    template<class... A> int            ActionDebugMask(A...)         { return 0; }
    template<class... A> bool           CheckGameSettingsChanged(A...){ return false; }
    template<class... A> void           ApplyGameSettingsChanged(A...){}
    template<class... A> void           ExitGame(A...)                {}
    template<class... A> void           CloseAllPlayersXuiScenes(A...){}
    template<class... A> void           CloseXuiScenes(A...)          {}
    template<class... A> void           CloseXuiScenesAndNavigateToScene(A...) {}
    template<class... A> void           AddCreditText(A...)           {}
    template<class... A> bool           AlreadySeenCreditText(A...)   { return false; }
    template<class... A> void           AdjustSplitscreenScene(A...)  {}
    template<class... A> void           AdjustSplitscreenScene_PlayerChanged(A...) {}
    template<class... A> void           EnableDebugOverlay(A...)      {}
    template<class... A> void           EnableMashupPackWorlds(A...)  {}
    template<class... A> void           DisplayNewDLCTip(A...)        {}
    template<class... A> int            FormatHTMLString(A...)        { return 0; }
    template<class... A> void           AddMemoryTPDFile(A...)        {}
    template<class... A> bool           DLCAlreadyPurchased(A...)     { return false; }
    template<class... A> bool           DLCInstalled(A...)            { return false; }
    template<class... A> void           DLCContentRetrieved(A...)     {}
    template<class... A> void           ClearDLCInstalled(A...)       {}
    template<class... A> void           ClearNewDLCAvailable(A...)    {}
    template<class... A> void           ClearAndResetDLCDownloadQueue(A...) {}
    template<class... A> void           Checkout(A...)                {}
    template<class... A> bool           CheckTMSDLCCanStop(A...)      { return false; }
    template<class... A> bool           CheckForEmptyStore(A...)      { return false; }
    template<class... A> bool           DownloadAlreadyPurchased(A...){ return false; }
    template<class... A> bool           GetBanListRead(A...)          { return false; }
    template<class... A> bool           GetBanListCheck(A...)         { return false; }
    template<class... A> bool           AutosaveDue(A...)             { return false; }
    template<class... A> int            SecondsToAutosave(A...)       { return 0; }
    template<class... A> void           LoadCommandBlockMenu(A...)    {}
    template<class... A> void           LoadContainerMenu(A...)       {}
    template<class... A> void           LoadHopperMenu(A...)          {}
    template<class... A> void           LoadAnvilMenu(A...)           {}
    template<class... A> void           LoadDispenserMenu(A...)       {}
    template<class... A> void           LoadFurnaceMenu(A...)         {}
    template<class... A> void           LoadEnchantmentMenu(A...)     {}
    template<class... A> void           LoadBeaconMenu(A...)          {}
    template<class... A> void           LoadBrewingStandMenu(A...)    {}
    template<class... A> void           LoadHorseInventoryMenu(A...)  {}
    template<class... A> void           LoadFireworksMenu(A...)       {}
    template<class... A> void           LoadInventoryMenu(A...)       {}
    template<class... A> const wchar_t* FormatChatMessage(A...)       { return L""; }
    // The store's offers aren't there to look a skin's up in
    template<class... A> bool           GetDLCFullOfferIDForSkinID(A...) { return false; }
    template<class... A> void           SetBanListCheck(A...)         {}
    template<class... A> bool           IsLocalMultiplayerAvailable(A...) { return false; }
    template<class... A> bool           GetBootedFromDiscPatch(A...)  { return false; }
    template<class... A> bool           GetChangingSessionType(A...)  { return false; }
    template<class... A> void*          GetCommerce(A...)             { return nullptr; }
    template<class... A> bool           GetCommerceCategoriesRetrieved(A...) { return false; }
    template<class... A> void*          GetCommerceCategory(A...)     { return nullptr; }
    template<class... A> bool           GetCommerceProductListInfoRetrieved(A...) { return false; }
    template<class... A> void*          GetCategoryInfo(A...)         { return nullptr; }
    template<class... A> const wchar_t* GetBDUsrDirPath(A...)         { return L""; }
    template<class... A> void           ClearSignInChangeUsersMask(A...) {}
    template<class... A> void           AddDLCRequest(A...)           {}
    template<class... A> void           AddTMSPPFileTypeRequest(A...) {}
    template<class... A> void           FreeLocalDLCImages(A...)      {}
    template<class... A> void           FreeLocalTMSFiles(A...)       {}
};

// Real upstream defines `CMinecraftApp app;` as a global instance of
// CMinecraftApp. This is a global stub instance that all upstream
// `app.X(...)` calls bind to. Inline so each TU gets its own; static
// link is not needed since the world-probe is a syntax-only build.
inline McleAppStub app;

// g_NetworkManager is the game's own CGameNetworkManager. Its members are
// in Game/MCLENetwork.cpp, the ones a host needs taken from
// Common/Network/GameNetworkManager.cpp.
#include "../Minecraft.Client/Common/Network/GameNetworkManager.h"

#endif // __cplusplus
