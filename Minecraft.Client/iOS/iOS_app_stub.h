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

class GAME_SETTINGS;
class C_4JProfile;
class CDLCManager;
struct LevelGenerationOptions;
class CGameSettingsManager;
class FileNameMap;

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
    };
    McleGameRulesStub m_gameRules;

    // Generic templates - accept any args, return either a default-
    // constructible value or void. Compile-only, not link-only.
    template<class... A> const wchar_t* GetString(A...)         { return L""; }
    template<class... A> int            DebugPrintf(A...)       { return 0; }
    template<class... A> void           FatalLoadError(A...)    {}
    template<class... A> int            GetGameSetting(A...)    { return 0; }
    template<class... A> GAME_SETTINGS* GetGameSettings(A...)   { return nullptr; }
    template<class... A> int            GetGameSettingsDebugMask(A...) { return 0; }
    template<class... A> int            GetGameHostOption(A...) { return 0; }
    template<class... A> void           SetGameHostOption(A...) {}
    template<class... A> int            GetGameNewHellScale(A...) { return 1; }
    template<class... A> int            GetGameNewWorldSize(A...) { return 0; }
    template<class... A> bool           GetGameNewWorldSizeUseMoat(A...) { return false; }
    template<class... A> int            GetHTMLColor(A...)      { return 0; }
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
    template<class... A> void           SetUniqueMapName(A...)        {}
    template<class... A> void           SetXuiServerAction(A...)      {}
    template<class... A> int            GetXuiServerAction(A...)      { return 0; }
    template<class... A> void*          GetXuiServerActionParam(A...) { return nullptr; }
    template<class... A> void           SetXuiServerActionParam(A...) {}
    template<class... A> void           EnterSaveNotificationSection(A...) {}
    template<class... A> void           LeaveSaveNotificationSection(A...) {}
    template<class... A> bool           IsSaveNotificationSection(A...) { return false; }
    template<class... A> void           CreateImageTextData(A...)     {}
    template<class... A> void           processSchematics(A...)       {}

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
};
inline McleNetworkManagerStub g_NetworkManager;

#endif // __cplusplus
