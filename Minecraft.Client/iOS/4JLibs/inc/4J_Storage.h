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
#include <vector>

#define MAX_SAVEFILENAME_LENGTH 64

// Current version of the dlc data creator
#define CURRENT_DLC_VERSION_NUM 3

#ifndef ERROR_IO_PENDING
#define ERROR_IO_PENDING 997L
#endif
#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND 2L
#endif

// Windows64's XCONTENT_DATA (x64headers/extraX64.h), what GetDLC hands back
#ifndef XCONTENT_MAX_DISPLAYNAME_LENGTH
#define XCONTENT_MAX_DISPLAYNAME_LENGTH 128
#define XCONTENT_MAX_FILENAME_LENGTH 42
#endif
typedef struct _XCONTENT_DATA {
    XCONTENTDEVICEID DeviceID;
    DWORD dwContentType;
    WCHAR szDisplayName[XCONTENT_MAX_DISPLAYNAME_LENGTH];
    CHAR szFileName[XCONTENT_MAX_FILENAME_LENGTH];
} XCONTENT_DATA, *PXCONTENT_DATA;

// The loaded world's save, written out by Game/MCLESaveList.cpp
extern "C" void *mcle_storage_allocate_save_data(unsigned int size);
extern "C" int  mcle_storage_save_save_data(void);
extern "C" void mcle_storage_reset_save_data(void);
extern "C" void mcle_storage_set_save_title(const wchar_t *title);
extern "C" void mcle_storage_set_save_images(const unsigned char *thumb, unsigned int thumbBytes,
                                             const unsigned char *text, unsigned int textBytes);

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
    // ConsoleSaveFileOriginal::Flush: the compressed save goes into a
    // buffer the storage owns, then SaveSaveData writes it to the world's
    // .ms file and calls back
    void *AllocateSaveData(unsigned int size) { return mcle_storage_allocate_save_data(size); }
    // Sets the thumbnail & image for the save, optionally setting the metadata in the png
    void SetSaveImages(PBYTE pbThumbnail, DWORD dwThumbnailBytes, PBYTE pbImage, DWORD dwImageBytes, PBYTE pbTextData,
                       DWORD dwTextDataBytes) {
        mcle_storage_set_save_images(pbThumbnail, dwThumbnailBytes, pbTextData, dwTextDataBytes);
    }
    // Call before a new save to clear out stored save file name
    void ResetSaveData() { mcle_storage_reset_save_data(); }
    // A new save's file is named after its title
    void SetSaveTitle(const wchar_t *pwchDefaultSaveName) { mcle_storage_set_save_title(pwchDefaultSaveName); }
    void SaveSaveData(int (*callback)(LPVOID, bool), LPVOID param) {
        bool ok = mcle_storage_save_save_data() != 0;
        if (callback) callback(param, ok);
    }
    template<class... A> bool         GetSaveUniqueNumber(A...) { return false; }
    template<class... A> std::wstring GetSaveUniqueFilename(A...) { return std::wstring(); }

    // Structs defined in the DLC_Creator, but added here to be used in the app
    typedef struct
    {
        unsigned int    uiFileSize;
        DWORD           dwType;
        DWORD           dwWchCount; // count of WCHAR in next array
        WCHAR           wchFile[1];
    }
    DLC_FILE_DETAILS, *PDLC_FILE_DETAILS;

    typedef struct
    {
        DWORD   dwType;
        DWORD   dwWchCount; // count of WCHAR in next array;
        WCHAR   wchData[1]; // will be an array of size dwBytes
    }
    DLC_FILE_PARAM, *PDLC_FILE_PARAM;
    // End of DLC_Creator structs

    enum EDLCStatus
    {
        EDLC_Error=0,
        EDLC_Idle,
        EDLC_NoOffers,
        EDLC_AlreadyEnumeratedAllOffers,
        EDLC_NoInstalledDLC,
        EDLC_Pending,
        EDLC_LoadInProgress,
        EDLC_Loaded,
        EDLC_ChangedDevice
    };

    // DLC, the packs in Documents/Windows64Media/DLC, a folder each like
    // the Windows64 install has them (Game/MCLEStorageDLC.cpp). The
    // callbacks come on a later pass of the main run loop, the way the
    // console storage's tick delivers them.
    C4JStorage::EDLCStatus GetInstalledDLC(int iPad, int (*Func)(LPVOID, int, int), LPVOID lpParam);
    XCONTENT_DATA &GetDLC(DWORD dw);
    DWORD MountInstalledDLC(int iPad, DWORD dwDLC, int (*Func)(LPVOID, int, DWORD, DWORD), LPVOID lpParam,
                            LPCSTR szMountDrive = nullptr);
    DWORD UnmountInstalledDLC(LPCSTR szMountDrive = nullptr);
    void GetMountedDLCFileList(const char *szMountDrive, std::vector<std::string> &fileList);
    std::string GetMountedPath(std::string szMount);

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
