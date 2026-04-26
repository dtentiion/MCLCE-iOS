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
};

// Real upstream defines `CMinecraftApp app;` as a global instance of
// CMinecraftApp. We provide a global stub instance that all upstream
// `app.X(...)` calls bind to. Inline so each TU gets its own; static
// link is not needed since the world-probe is a syntax-only build.
inline McleAppStub app;

#endif // __cplusplus
