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

class GAME_SETTINGS;
class C_4JProfile;
class CDLCManager;
class LevelGenerationOptions;
class CGameSettingsManager;
class FileNameMap;

struct McleAppStub {
    // Heavyweight returning ptr to opaque object; nullptr is fine
    // for most call sites that just check for non-null OR pass it
    // forward.
    template<class T> T* getOpaque() { return nullptr; }

    // Direct fields upstream uses without parens.
    void* m_dlcManager = nullptr;
    void* vSkinNames   = nullptr;

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
    template<class... A> void*          GetMojangDataForXuid(A...) { return nullptr; }
    template<class... A> int            GetTerrainFeaturePosition(A...) { return 0; }
    template<class... A> void           AddTerrainFeaturePosition(A...) {}
    template<class... A> int            GetAdditionalModelParts(A...) { return 0; }
    template<class... A> int            GetAnimOverrideBitmask(A...)  { return 0; }
    template<class... A> void           SetAnimOverrideBitmask(A...)  {}
    template<class... A> void           SetAdditionalSkinBoxes(A...)  {}
    template<class... A> void           SetUniqueMapName(A...)        {}
    template<class... A> void           SetXuiServerAction(A...)      {}
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

#endif // __cplusplus
