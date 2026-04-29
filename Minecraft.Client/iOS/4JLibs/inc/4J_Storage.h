// iOS shim for the platform 4J storage / save-game layer. Real storage
// work lives in Minecraft.Client/iOS/Storage/ on top of NSFileManager.
//
// The class shape mirrors upstream's Orbis / Windows 4J_Storage.h so
// upstream `C4JStorage::EMessageResult result` parses match. The
// concrete path methods forward to STO_iOS_Paths C entry points so
// upstream gameplay code that calls StorageManager.GetSaveRootPath()
// gets the real sandboxed iOS Documents directory.

#pragma once

#include "iOS_WinCompat.h"
#include "../../Storage/STO_iOS_Paths.h"

#ifdef __cplusplus
#include <string>

#define MAX_SAVEFILENAME_LENGTH 64

// Real SAVE_DETAILS lives in the platform 4J_Storage lib (Xbox/Sony). On
// iOS we only need a forward decl so upstream UI headers that store
// `SAVE_DETAILS *m_pSaveDetails` parse.
struct SAVE_DETAILS;

class C4JStorage
{
public:
    enum EGlobalStorage {
        eGlobalStorage_Title = 0,
        eGlobalStorage_TitleUser,
        eGlobalStorage_Max,
    };

    enum EMessageResult {
        EMessage_Undefined = 0,
        EMessage_Busy,
        EMessage_Pending,
        EMessage_Cancelled,
        EMessage_ResultAccept,
        EMessage_ResultDecline,
        EMessage_ResultThirdOption,
        EMessage_ResultFourthOption,
    };

    enum ESaveGameState {
        ESaveGame_Idle = 0,
        ESaveGame_Save,
        ESaveGame_SaveCompleteSuccess,
        ESaveGame_SaveCompleteFail,
        ESaveGame_SaveIncomplete,
        ESaveGame_SaveIncomplete_WaitingOnResponse,
        ESaveGame_SaveSubfiles,
        ESaveGame_SaveSubfilesCompleteSuccess,
        ESaveGame_SaveSubfilesCompleteFail,
        ESaveGame_SaveSubfilesIncomplete,
    };

    template<class... A> bool GetSaveDisabled(A...)        { return false; }
    template<class... A> bool IsSaving(A...)               { return false; }
    template<class... A> int  GetActiveSaveSlot(A...)      { return 0; }
    template<class... A> void* GetActiveSaveFile(A...)     { return nullptr; }

    // Real iOS path lookups. These forward to STO_iOS_Paths so any
    // upstream code that walks `StorageManager.GetSaveRootPath()` lands
    // on the sandboxed Documents directory rather than a stub.
    const char* GetSaveRootPath()    { return ios_documents_dir(); }
    const char* GetGameHDDRootPath() { return ios_gamehdd_dir(); }
    const char* GetCachesPath()      { return ios_caches_dir(); }
    const char* GetAppSupportPath()  { return ios_app_support_dir(); }
    const char* GetBundlePath()      { return ios_bundle_resources_dir(); }

    // Save-buffer helpers ConsoleSaveFileOriginal calls when constructed
    // with no in-memory bytes (Xbox/PS path picks up the OS-resident save).
    // iOS hands raw bytes to the ctor directly via mcle_ios_game so these
    // safe defaults are never reached at runtime; the symbols just need
    // to exist at compile time.
    template<class... A> unsigned int GetSaveSize(A...)         { return 0; }
    template<class... A> void*        GetSaveData(A...)         { return nullptr; }
    template<class... A> void*        AllocateSaveData(A...)    { return nullptr; }
    template<class... A> bool         GetSaveUniqueNumber(A...) { return false; }
    template<class... A> std::wstring GetSaveUniqueFilename(A...) { return std::wstring(); }
    template<class... A> int          DLC_FILE_PARAM(A...)         { return 0; }

    enum ESavingMessage {
        ESavingMessage_None    = 0,
        ESavingMessage_Saving  = 1,
        ESavingMessage_Loading = 2,
    };
};

// Real upstream 4J_Storage.h declares `extern C4JStorage StorageManager;`
// Mirror that so platform-agnostic code referencing the global resolves.
// The actual instance is defined in probe_stub.cpp.
extern C4JStorage StorageManager;

#endif // __cplusplus
