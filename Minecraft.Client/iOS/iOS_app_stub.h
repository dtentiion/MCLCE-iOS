// Compile-only stub for the upstream global `CMinecraftApp app`.
// Real CMinecraftApp lives in Minecraft.Client/Common/Consoles_App.h
// and brings in ~half the Minecraft.Client tree. For the world-probe
// we only need every `app.X(...)` call site to typecheck. Method
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
#include <os/log.h>

class GAME_SETTINGS;
class C_4JProfile;
class CDLCManager;
struct LevelGenerationOptions;
class CGameSettingsManager;
class FileNameMap;

extern "C" int mcle_log_msg(const char *msg);
extern "C" unsigned char mcle_settings_get(int setting); // MCLE_iOS_Settings.mm
extern "C" const wchar_t *mcle_wstring(int id);        // Game/MCLEStrings.cpp
extern "C" int mcle_html_colour(int colour);           // Game/MCLEContainerMenu.cpp

struct McleAppStub {
    // Heavyweight returning ptr to opaque object; nullptr is fine
    // for most call sites that just check for non-null OR pass it
    // forward.
    template<class T> T* getOpaque() { return nullptr; }

    // Direct fields upstream uses without parens.
    // Real Consoles_App.h:79 has `DLCManager m_dlcManager;` (by value).
    // Player.cpp does `app.m_dlcManager.getSkinFile(...)` so we need the
    // real DLCManager type, complete. iOS_stdafx.h pre-includes
    // DLCFile.h -> DLCManager.h before this header so the type is
    // visible.
    DLCManager m_dlcManager;
    // Real `std::vector<wstring> vSkinNames` from Consoles_App.h.
    std::vector<std::wstring> vSkinNames;
    // Real Consoles_App.h:737 has `GameRuleManager m_gameRules;` by
    // value. MinecraftServer.cpp reads members of it directly. The
    // probe doesn't run game-rule logic; an empty stub class with the
    // members upstream touches is enough for compile.
    struct McleGameRulesStub {
        template<class... A> void* getLevelGenerators(A...)        { return nullptr; }
        template<class... A> void* getLevelGenerationOptions(A...) { return nullptr; }
        template<class... A> class LevelRuleset* getGameRuleDefinitions(A...) { return nullptr; }
        template<class... A> void  loadFromBytes(A...)             {}
        template<class... A> void  saveToBytes(A...)               {}
        template<class... A> void  saveGameRules(A...)             {}
        template<class... A> void  loadGameRules(A...)             {}
        template<class... A> void  applyGameRules(A...)            {}
        template<class... A> void  resetGameRules(A...)            {}
        template<class... A> void  unloadCurrentGameRules(A...)    {}
        template<class... A> void  loadCurrentGameRules(A...)      {}
    };
    McleGameRulesStub m_gameRules;

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
    // Our stub now provides both so static_cast<float>(GetGameSettings(p,s))
    // resolves to int->float instead of pointer->float.
    GAME_SETTINGS *GetGameSettings(int /*pad*/)               { return nullptr; }
    // The iOS settings store (MCLE_iOS_Settings.mm) keeps the eGameSetting
    // order, so the player's settings reach the upstream code that reads
    // them (clouds, Display HUD / Hand, skin animation)
    int            GetGameSettings(int /*pad*/, int setting) {
        if (setting >= 0 && setting < 32) return mcle_settings_get(setting);
        return 0;
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
    template<class... A> int            GetGameNewHellScale(A...) { return 1; }
    template<class... A> int            GetGameNewWorldSize(A...) { return 0; }
    template<class... A> bool           GetGameNewWorldSizeUseMoat(A...) { return false; }
    // CMinecraftApp::GetHTMLColour, the texture pack's colour table
    template<class T> int               GetHTMLColor(T colour)  { return mcle_html_colour(static_cast<int>(colour)); }
    template<class... A> bool           GetMobsDontAttackEnabled(A...) { return false; }
    template<class... A> bool           GetMobsDontTickEnabled(A...)   { return false; }
    template<class... A> bool           GetWriteSavesToFolderEnabled(A...) { return false; }
    template<class... A> bool           GetResetNether(A...)    { return false; }
    template<class... A> bool           DebugArtToolsOn(A...)   { return false; }
    template<class... A> bool           DebugSettingsOn(A...)   { return false; }
    template<class... A> bool           DefaultCapeExists(A...) { return false; }
    template<class... A> bool           IsFileInMemoryTextures(A...) { return false; }
    template<class... A> bool           isXuidNotch(A...)       { return false; }
    template<class... A> int64_t        getAppTime(A...)        { return 0; }
    template<class... A> int            getSkinPathFromId(A...) { return 0; }
    template<class... A> LevelGenerationOptions* getLevelGenerationOptions(A...) { return nullptr; }
    template<class... A> void*          GetSaveThumbnail(A...)  { return nullptr; }
    // Real signature returns `MOJANG_DATA*` - declared in stdafx.
    template<class... A> struct MOJANG_DATA* GetMojangDataForXuid(A...) { return nullptr; }
    template<class... A> int            GetTerrainFeaturePosition(A...) { return 0; }
    template<class... A> void           AddTerrainFeaturePosition(A...) {}
    // Real upstream return type is `std::vector<ModelPart*>*`. ModelPart
    // is forward-declared so the pointer type composes; the body returns
    // nullptr (Player.cpp checks for null before iterating).
    template<class... A> std::vector<class ModelPart*>* GetAdditionalModelParts(A...) { return nullptr; }
    template<class... A> int            GetAnimOverrideBitmask(A...)  { return 0; }
    template<class... A> void           SetAnimOverrideBitmask(A...)  {}
    // Real upstream returns `vector<ModelPart*>*`. Player.cpp does
    // `m_ppAdditionalModelParts = app.SetAdditionalSkinBoxes(...)`.
    template<class... A> std::vector<class ModelPart*>* SetAdditionalSkinBoxes(A...) { return nullptr; }
    template<class... A> std::vector<class ModelPart*>* GetAdditionalSkinBoxes(A...) { return nullptr; }
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
    template<class... A> int            GetTutorialMode(A...)         { return 0; }
    template<class... A> void           SetTutorialMode(A...)         {}
    template<class... A> bool           IsTutorialActive(A...)        { return false; }
    template<class... A> bool           IsTrialMode(A...)             { return false; }
    template<class... A> bool           IsDemoMode(A...)              { return false; }
    template<class... A> int            CreateImageTextData(A...)     { return 0; }
    template<class... A> void           processSchematics(A...)       {}
    template<class... A> void           processSchematicsLighting(A...) {}
    // English only for now: the tail of CMinecraftApp::getLocale, which
    // always ends with en-US then en-EN.
    void getLocale(std::vector<std::wstring> &out) { out.push_back(L"en-US"); out.push_back(L"en-EN"); }
    template<class... A> bool           isXuidDeadmau5(A...)          { return false; }
    template<class... A> void*          getArchiveFile(A...)          { return nullptr; }
    template<class... A> int            GetLocalPlayerCount(A...)     { return 1; }
    template<class... A> int            getSkinIdFromPath(A...)       { return 0; }
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
    template<class... A> void           AddMemoryTextureFile(A...)    {}
    template<class... A> void           ClearTerrainFeaturePosition(A...) {}
    // Return LevelRuleset* so PlayerList::placeNewPlayer (and similar)
    // can call ->postProcessPlayer(player) on it. nullptr is fine - the
    // call site checks for non-null first.
    template<class... A> class LevelRuleset* getGameRuleDefinitions(A...)  { return nullptr; }
    template<class... A> bool           DLCAlreadyPurchased(A...)     { return false; }
    template<class... A> bool           DLCInstalled(A...)            { return false; }
    template<class... A> bool           DLCInstallPending(A...)       { return false; }
    template<class... A> void           DLCContentRetrieved(A...)     {}
    template<class... A> void           DLCInstallProcessCompleted(A...) {}
    template<class... A> void           ClearDLCInstalled(A...)       {}
    template<class... A> void           ClearNewDLCAvailable(A...)    {}
    template<class... A> void           ClearAndResetDLCDownloadQueue(A...) {}
    template<class... A> void           Checkout(A...)                {}
    template<class... A> bool           CheckTMSDLCCanStop(A...)      { return false; }
    template<class... A> bool           CheckForEmptyStore(A...)      { return false; }
    template<class... A> bool           DownloadAlreadyPurchased(A...){ return false; }
    template<class... A> bool           GetBanListRead(A...)          { return false; }
    template<class... A> bool           GetBanListCheck(A...)         { return false; }
    template<class... A> int            GetXuiAction(A...)            { return 0; }
    template<class... A> bool           AutosaveDue(A...)             { return false; }
    template<class... A> void           SetAction(A...)               {}
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
    template<class... A> bool           GetMemFileDetails(A...)       { return false; }
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
// CMinecraftApp. We provide a global stub instance that all upstream
// `app.X(...)` calls bind to. Inline so each TU gets its own; static
// link is not needed since the world-probe is a syntax-only build.
inline McleAppStub app;

// Stub for the upstream global `g_NetworkManager`. Connection.cpp /
// Socket.cpp reach into it during the receive-loop. Probe never runs
// the receive loop; variadic template members absorb signatures and
// every getter returns sane null/false defaults.
struct McleNetworkManagerStub {
    template<class... A> bool IsLeavingGame(A...)        { return false; }
    template<class... A> bool IsInSession(A...)          { return false; }
    template<class... A> bool IsHost(A...)               { return false; }
    template<class... A> bool IsLocalGame(A...)          { return true; }
    template<class... A> int  GetSmallId(A...)           { return 0; }
    template<class... A> McleNetworkManagerStub* GetHostPlayer(A...) { return this; }
    // Real upstream returns INetworkPlayer*. NetworkPlayerInterface.h
    // is pre-included in iOS_stdafx so the type is visible.
    template<class... A> class INetworkPlayer* GetPlayerByXuid(A...)            { return nullptr; }
    template<class... A> class INetworkPlayer* GetPlayerBySmallId(A...)         { return nullptr; }
    template<class... A> class INetworkPlayer* GetLocalPlayerByUserIndex(A...)  { return nullptr; }
    template<class... A> void  ServerReady(A...)         {}
    template<class... A> void  ServerStopped(A...)       {}
    template<class... A> void  ClientReady(A...)         {}
    template<class... A> void  ClientStopped(A...)       {}
    template<class... A> void  Disconnect(A...)          {}
    template<class... A> bool  IsServer(A...)            { return false; }
    template<class... A> bool  IsClient(A...)            { return false; }
    template<class... A> int   GetNumPlayers(A...)       { return 0; }
    template<class... A> int   GetMaxPlayers(A...)       { return 1; }
    template<class... A> int   GetSessionState(A...)     { return 0; }
    template<class... A> bool  SystemFlagGet(A...)       { return false; }
    template<class... A> void  SystemFlagSet(A...)       {}
    template<class... A> int   GetPlayerCount(A...)      { return 0; }
    template<class... A> bool  SessionHasSpace(A...)     { return true; }
    template<class... A> int   GetLocalPlayerCount(A...) { return 0; }
    template<class... A> int   GetRemotePlayerCount(A...) { return 0; }
    template<class... A> int   GetOnlinePlayerCount(A...) { return 0; }
    template<class... A> class INetworkPlayer* GetPlayerByIndex(A...) { return nullptr; }
    template<class... A> class INetworkPlayer* GetPlayerBySessionIndex(A...) { return nullptr; }
    template<class... A> class INetworkPlayer* AddLocalPlayerByUserIndex(A...) { return nullptr; }
    template<class... A> void  RemoveLocalPlayerByUserIndex(A...) {}
    template<class... A> void  AddLocalPlayer(A...) {}
    template<class... A> void  RemoveLocalPlayer(A...) {}
    template<class... A> void  AddRemotePlayer(A...) {}
    template<class... A> void  RemoveRemotePlayer(A...) {}
    template<class... A> int   GetSendQueueSizeMessages(A...) { return 0; }
    template<class... A> int   GetSendQueueSizeBytes(A...)    { return 0; }
};
inline McleNetworkManagerStub g_NetworkManager;

#endif // __cplusplus
