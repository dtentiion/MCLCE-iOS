// iOS stub for the platform 4J storage / save-game layer. Real storage
// work lives in Minecraft.Client/iOS/Storage/ on top of NSFileManager.
// This header exists so upstream gameplay code that includes it from
// stdafx.h does not fail to find the file.
//
// The class shape mirrors upstream's Orbis / Windows 4J_Storage.h so
// upstream `C4JStorage::EMessageResult result` parses match. Method
// bodies are stubs - real iOS storage layer is in Storage/.

#pragma once

#include "iOS_WinCompat.h"

#ifdef __cplusplus

#define MAX_SAVEFILENAME_LENGTH 64

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
};

// Real upstream 4J_Storage.h declares `extern C4JStorage StorageManager;`
// Mirror that so platform-agnostic code referencing the global resolves.
// The actual instance is defined in probe_stub.cpp.
extern C4JStorage StorageManager;

#endif // __cplusplus
