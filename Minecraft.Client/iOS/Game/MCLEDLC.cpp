// CMinecraftApp's DLC install and memory texture files (Common/Consoles_App.cpp)
// for the app stub, and the widening of a .pck's strings DLCManager needs on
// iOS (iOS_DLC.h).

#include "MCLEDLC.h"
#include "../iOS_DLC.h"

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../../../upstream/Minecraft.Client/Common/DLC/DLCManager.h"
#include "../../../upstream/Minecraft.Client/Common/DLC/DLCPack.h"
#include "../../../upstream/Minecraft.Client/EntityRenderDispatcher.h"
#include "../../../upstream/Minecraft.Client/EntityRenderer.h"
#include "../../../upstream/Minecraft.Client/Model.h"

void mcle_ui_handle_dlc_mounting_complete(); // MCLESkinSelectMenu.cpp

void mcle_dlc_install_if_needed(int iPad) {
    if (!app.DLCInstallProcessCompleted()) app.StartInstallDLCProcess(iPad);
}

bool McleAppStub::StartInstallDLCProcess(int iPad) {
    app.DebugPrintf("--- CMinecraftApp::StartInstallDLCProcess: pad=%i.\n", iPad);

    // If there is already a call to this in progress, then do nothing
    // If the app says dlc is installed, then there has been no new system message to tell us there's new DLC since the last call to StartInstallDLCProcess
    if ((app.DLCInstallProcessCompleted() == false) && (m_bDLCInstallPending == false)) {
        app.m_dlcManager.resetUnnamedCorruptCount();
        m_bDLCInstallPending = true;
        m_iTotalDLC = 0;
        m_iTotalDLCInstalled = 0;
        app.DebugPrintf("--- CMinecraftApp::StartInstallDLCProcess - StorageManager.GetInstalledDLC\n");

        StorageManager.GetInstalledDLC(iPad, &McleAppStub::DLCInstalledCallback, this);
        return true;
    } else {
        app.DebugPrintf("--- CMinecraftApp::StartInstallDLCProcess - nothing to do\n");

        return false;
    }
}

// Installed DLC callback
int McleAppStub::DLCInstalledCallback(LPVOID pParam, int iInstalledC, int iPad) {
    app.DebugPrintf("--- CMinecraftApp::DLCInstalledCallback: totalDLC=%i, pad=%i.\n", iInstalledC, iPad);
    app.m_iTotalDLC = iInstalledC;
    app.MountNextDLC(iPad);
    return 0;
}

void McleAppStub::MountNextDLC(int iPad) {
    app.DebugPrintf("--- CMinecraftApp::MountNextDLC: pad=%i.\n", iPad);
    if (m_iTotalDLCInstalled < m_iTotalDLC) {
        // Mount it
        // We also need to match the ones the user wants to mount with the installed DLC
        // We're supposed to use a generic save game as a cache of these to do this, with XUSER_ANY

        if (StorageManager.MountInstalledDLC(iPad, m_iTotalDLCInstalled, &McleAppStub::DLCMountedCallback, this) !=
            ERROR_IO_PENDING) {
            // corrupt DLC
            app.DebugPrintf("Failed to mount DLC %d for pad %d\n", m_iTotalDLCInstalled, iPad);
            ++m_iTotalDLCInstalled;
            app.MountNextDLC(iPad);
        } else {
            app.DebugPrintf("StorageManager.MountInstalledDLC ok\n");
        }
    } else {
        m_bDLCInstallPending = false;
        m_bDLCInstallProcessCompleted = true;

        // ui.HandleDLCMountingComplete
        mcle_ui_handle_dlc_mounting_complete();
    }
}

int McleAppStub::DLCMountedCallback(LPVOID pParam, int iPad, DWORD dwErr, DWORD dwLicenceMask) {
    app.DebugPrintf("--- CMinecraftApp::DLCMountedCallback\n");

    if (dwErr != ERROR_SUCCESS) {
        // corrupt DLC
        app.DebugPrintf("Failed to mount DLC for pad %d: %d\n", iPad, dwErr);
        app.m_dlcManager.incrementUnnamedCorruptCount();
    } else {
        XCONTENT_DATA ContentData = StorageManager.GetDLC(app.m_iTotalDLCInstalled);

        DLCPack *pack = app.m_dlcManager.getPack(ContentData.szDisplayName);

        if (pack != nullptr && pack->IsCorrupt()) {
            app.DebugPrintf("Pack '%ls' is corrupt, removing it from the DLC Manager.\n", ContentData.szDisplayName);

            app.m_dlcManager.removePack(pack);
            pack = nullptr;
        }

        if (pack == nullptr) {
            app.DebugPrintf("Pack \"%ls\" is not installed, so adding it\n", ContentData.szDisplayName);

            pack = new DLCPack(ContentData.szDisplayName, dwLicenceMask);
            pack->SetDLCMountIndex(app.m_iTotalDLCInstalled);
            pack->SetDLCDeviceID(ContentData.DeviceID);
            app.m_dlcManager.addPack(pack);

            app.HandleDLC(pack);

            // Windows64 hands a pack with textures to the texture pack
            // repository here. TexturePackRepository isn't built, so the pack
            // stays with the DLC manager only.
        } else {
            app.DebugPrintf("Pack \"%ls\" is already installed. Updating license to %d\n", ContentData.szDisplayName,
                            dwLicenceMask);

            pack->SetDLCMountIndex(app.m_iTotalDLCInstalled);
            pack->SetDLCDeviceID(ContentData.DeviceID);
            pack->updateLicenseMask(dwLicenceMask);
        }

        StorageManager.UnmountInstalledDLC();
    }
    ++app.m_iTotalDLCInstalled;
    app.MountNextDLC(iPad);

    return 0;
}

void McleAppStub::HandleDLC(DLCPack *pack) {
    DWORD dwFilesProcessed = 0;
    std::vector<std::string> dlcFilenames;
    StorageManager.GetMountedDLCFileList("DLCDrive", dlcFilenames);
    for (size_t i = 0; i < dlcFilenames.size(); i++) {
        m_dlcManager.readDLCDataFile(dwFilesProcessed, dlcFilenames[i], pack);
    }

    if (dwFilesProcessed == 0) m_dlcManager.removePack(pack);
}

namespace {

// Consoles_App's MEMDATA
struct MemData {
    PBYTE pbData;
    DWORD dwBytes;
    unsigned char ucRefCount;
};

std::mutex g_memFilesLock;
std::unordered_map<std::wstring, MemData *> g_memFiles;

} // namespace

void McleAppStub::AddMemoryTextureFile(const std::wstring &wName, PBYTE pbData, DWORD dwBytes) {
    std::lock_guard<std::mutex> lock(g_memFilesLock);
    // check it's not already in
    MemData *pData = nullptr;
    auto it = g_memFiles.find(wName);
    if (it != g_memFiles.end()) {
        pData = (*it).second;

        if (pData->dwBytes == 0 && dwBytes != 0) {
            // This should never be nullptr if dwBytes is 0
            if (pData->pbData != nullptr) delete[] pData->pbData;

            pData->pbData = pbData;
            pData->dwBytes = dwBytes;
        }

        ++pData->ucRefCount;
        return;
    }

    // this is a texture (png) file

    // add this texture to the list of memory texture files - it will then be picked up by the level renderer's AddEntity

    pData = new MemData();
    pData->pbData = pbData;
    pData->dwBytes = dwBytes;
    pData->ucRefCount = 1;

    // use the xuid to access the skin data
    g_memFiles[wName] = pData;
}

void McleAppStub::RemoveMemoryTextureFile(const std::wstring &wName) {
    std::lock_guard<std::mutex> lock(g_memFilesLock);

    auto it = g_memFiles.find(wName);
    if (it != g_memFiles.end()) {
        MemData *pData = (*it).second;
        --pData->ucRefCount;
        if (pData->ucRefCount <= 0) {
            delete pData;
            g_memFiles.erase(wName);
        }
    }
}

bool McleAppStub::IsFileInMemoryTextures(const std::wstring &wName) {
    std::lock_guard<std::mutex> lock(g_memFilesLock);
    return g_memFiles.find(wName) != g_memFiles.end();
}

void McleAppStub::GetMemFileDetails(const std::wstring &wName, PBYTE *ppbData, DWORD *pdwBytes) {
    std::lock_guard<std::mutex> lock(g_memFilesLock);
    auto it = g_memFiles.find(wName);
    if (it != g_memFiles.end()) {
        MemData *pData = (*it).second;
        *ppbData = pData->pbData;
        *pdwBytes = pData->dwBytes;
    }
}

namespace {

std::mutex g_additionalModelPartsLock;
std::mutex g_additionalSkinBoxesLock;
std::mutex g_animOverrideBitmaskLock;
std::unordered_map<DWORD, std::vector<ModelPart *> *> g_additionalModelParts;
std::unordered_map<DWORD, std::vector<SKIN_BOX *> *> g_additionalSkinBoxes;
std::unordered_map<DWORD, unsigned int> g_animOverrides;

// The player renderer's model. It's made with the game's renderer, which
// the port doesn't have until a world has loaded.
Model *playerModel() {
    if (EntityRenderDispatcher::instance == nullptr) return nullptr;
    EntityRenderer *renderer = EntityRenderDispatcher::instance->getRenderer(eTYPE_PLAYER);
    return renderer ? renderer->getModel() : nullptr;
}

} // namespace

void McleAppStub::SetAdditionalSkinBoxes(DWORD dwSkinID, SKIN_BOX *SkinBoxA, DWORD dwSkinBoxC) {
    Model *pModel = playerModel();
    if (pModel == nullptr) return;
    std::vector<ModelPart *> *pvModelPart = new std::vector<ModelPart *>;
    std::vector<SKIN_BOX *> *pvSkinBoxes = new std::vector<SKIN_BOX *>;

    std::lock_guard<std::mutex> lockParts(g_additionalModelPartsLock);
    std::lock_guard<std::mutex> lockBoxes(g_additionalSkinBoxesLock);

    app.DebugPrintf("*** SetAdditionalSkinBoxes - Inserting model parts for skin %d from array of Skin Boxes\n",
                    dwSkinID & 0x0FFFFFFF);

    // convert the skin boxes into model parts, and add to the humanoid model
    for (unsigned int i = 0; i < dwSkinBoxC; i++) {
        ModelPart *pModelPart = pModel->AddOrRetrievePart(&SkinBoxA[i]);
        pvModelPart->push_back(pModelPart);
        pvSkinBoxes->push_back(&SkinBoxA[i]);
    }

    g_additionalModelParts.insert(std::pair<DWORD, std::vector<ModelPart *> *>(dwSkinID, pvModelPart));
    g_additionalSkinBoxes.insert(std::pair<DWORD, std::vector<SKIN_BOX *> *>(dwSkinID, pvSkinBoxes));
}

std::vector<ModelPart *> *McleAppStub::SetAdditionalSkinBoxes(DWORD dwSkinID, std::vector<SKIN_BOX *> *pvSkinBoxA) {
    Model *pModel = playerModel();
    if (pModel == nullptr) return nullptr;
    std::vector<ModelPart *> *pvModelPart = new std::vector<ModelPart *>;

    std::lock_guard<std::mutex> lockParts(g_additionalModelPartsLock);
    std::lock_guard<std::mutex> lockBoxes(g_additionalSkinBoxesLock);
    app.DebugPrintf("*** SetAdditionalSkinBoxes - Inserting model parts for skin %d from array of Skin Boxes\n",
                    dwSkinID & 0x0FFFFFFF);

    // convert the skin boxes into model parts, and add to the humanoid model
    for (auto &it : *pvSkinBoxA) {
        ModelPart *pModelPart = pModel->AddOrRetrievePart(it);
        pvModelPart->push_back(pModelPart);
    }

    g_additionalModelParts.emplace(dwSkinID, pvModelPart);
    g_additionalSkinBoxes.emplace(dwSkinID, pvSkinBoxA);
    return pvModelPart;
}

std::vector<ModelPart *> *McleAppStub::GetAdditionalModelParts(DWORD dwSkinID) {
    std::lock_guard<std::mutex> lock(g_additionalModelPartsLock);
    auto it = g_additionalModelParts.find(dwSkinID);
    return it != g_additionalModelParts.end() ? it->second : nullptr;
}

std::vector<SKIN_BOX *> *McleAppStub::GetAdditionalSkinBoxes(DWORD dwSkinID) {
    std::lock_guard<std::mutex> lock(g_additionalSkinBoxesLock);
    auto it = g_additionalSkinBoxes.find(dwSkinID);
    return it != g_additionalSkinBoxes.end() ? it->second : nullptr;
}

unsigned int McleAppStub::GetAnimOverrideBitmask(DWORD dwSkinID) {
    std::lock_guard<std::mutex> lock(g_animOverrideBitmaskLock);
    auto it = g_animOverrides.find(dwSkinID);
    return it != g_animOverrides.end() ? it->second : 0;
}

void McleAppStub::SetAnimOverrideBitmask(DWORD dwSkinID, unsigned int uiAnimOverrideBitmask) {
    // Make thread safe
    std::lock_guard<std::mutex> lock(g_animOverrideBitmaskLock);
    // already in here, the first one stays
    g_animOverrides.insert(std::pair<DWORD, unsigned int>(dwSkinID, uiAnimOverrideBitmask));
}

namespace {

// Reads a .pck as the DLC_Creator wrote it: little endian, or big endian
// for the PS3 packs, strings as UTF-16 in DLC_FILE_PARAM / DLC_FILE_DETAILS
// laid out with Windows' 2 byte WCHAR.
class PckReader {
public:
    PckReader(const BYTE *data, DWORD length, bool swap) : m_data(data), m_length(length), m_swap(swap) {}

    bool u32(unsigned int &out) {
        if (m_pos + 4 > m_length) return false;
        unsigned int v;
        memcpy(&v, m_data + m_pos, 4);
        if (m_swap) v = DLCManager::SwapInt32(v);
        out = v;
        m_pos += 4;
        return true;
    }

    // count UTF-16 units, then the rest of Windows' struct: sizeof was
    // taken with one WCHAR in it and padded to 4. The app reads them as
    // C strings, so they end at the first null.
    bool string(unsigned int count, size_t structTail, std::wstring &out) {
        size_t bytes = static_cast<size_t>(count) * 2;
        if (m_pos + bytes + structTail > m_length) return false;
        out.clear();
        for (unsigned int i = 0; i < count; ++i) {
            unsigned int unit = readUnit(m_pos + i * 2);
            if (unit == 0) break;
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < count) {
                unsigned int low = readUnit(m_pos + (i + 1) * 2);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    out.push_back(static_cast<wchar_t>(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00)));
                    ++i;
                    continue;
                }
            }
            out.push_back(static_cast<wchar_t>(unit));
        }
        m_pos += bytes + structTail;
        return true;
    }

    bool bytes(DWORD count, const BYTE *&out) {
        if (m_pos + count > m_length) return false;
        out = m_data + m_pos;
        m_pos += count;
        return true;
    }

private:
    unsigned int readUnit(size_t at) {
        unsigned int lo = m_data[at], hi = m_data[at + 1];
        return m_swap ? ((lo << 8) | hi) : (lo | (hi << 8));
    }

    const BYTE *m_data;
    DWORD m_length;
    bool m_swap;
    size_t m_pos = 0;
};

// Windows' sizeof(DLC_FILE_PARAM) and sizeof(DLC_FILE_DETAILS), less the
// fields before the string
const size_t kWinParamTail = 12 - 8;
const size_t kWinDetailsTail = 16 - 12;

void put32(std::vector<BYTE> &out, unsigned int v) {
    BYTE b[4];
    memcpy(b, &v, 4);
    out.insert(out.end(), b, b + 4);
}

// The string the way this build's structs hold it: dwWchCount wchar_t, then
// the WCHAR the struct is declared with, left as the terminator
void putString(std::vector<BYTE> &out, const std::wstring &s, size_t structHead, size_t structSize) {
    const size_t at = out.size();
    out.resize(at + structSize - structHead + s.size() * sizeof(WCHAR), 0);
    if (!s.empty()) memcpy(&out[at], s.data(), s.size() * sizeof(WCHAR));
}

struct Param {
    unsigned int type;
    std::wstring value;
};

bool readParams(PckReader &in, unsigned int count, std::vector<Param> &params) {
    params.resize(count);
    for (Param &p : params) {
        unsigned int wchCount;
        if (!in.u32(p.type) || !in.u32(wchCount) || !in.string(wchCount, kWinParamTail, p.value)) return false;
    }
    return true;
}

void writeParams(std::vector<BYTE> &out, const std::vector<Param> &params) {
    for (const Param &p : params) {
        put32(out, p.type);
        put32(out, static_cast<unsigned int>(p.value.size()));
        putString(out, p.value, 8, sizeof(C4JStorage::DLC_FILE_PARAM));
    }
}

// A .pck (DLCManager::processDLCDataFile's layout) or the audio file inside
// one (DLCAudioFile's, the same without the XML version or nested packs)
bool widen(const BYTE *data, DWORD length, bool audio, std::vector<BYTE> &out) {
    if (length < 4) return false;
    unsigned int version;
    memcpy(&version, data, 4);
    bool swap = false;
    if (!audio && version > CURRENT_DLC_VERSION_NUM) {
        if (DLCManager::SwapInt32(version) > CURRENT_DLC_VERSION_NUM) return false;
        swap = true;
    }
    PckReader in(data, length, swap);

    unsigned int paramCount;
    std::vector<Param> params;
    if (!in.u32(version) || !in.u32(paramCount) || !readParams(in, paramCount, params)) return false;
    put32(out, version);
    put32(out, paramCount);
    writeParams(out, params);

    if (!audio) {
        bool xmlVersion = false;
        for (const Param &p : params) {
            if (DLCManager::getParameterType(p.value) == DLCManager::e_DLCParamType_XMLVersion) xmlVersion = true;
        }
        if (xmlVersion) {
            unsigned int skip;
            if (!in.u32(skip)) return false;
            put32(out, skip);
        }
    }

    unsigned int fileCount;
    if (!in.u32(fileCount)) return false;
    struct File {
        unsigned int size, type;
        std::wstring name;
        std::vector<Param> params;
        std::vector<BYTE> data;
    };
    std::vector<File> files(fileCount);
    for (File &f : files) {
        unsigned int wchCount;
        if (!in.u32(f.size) || !in.u32(f.type) || !in.u32(wchCount) || !in.string(wchCount, kWinDetailsTail, f.name)) {
            return false;
        }
    }
    for (File &f : files) {
        unsigned int count;
        const BYTE *blob;
        if (!in.u32(count) || !readParams(in, count, f.params) || !in.bytes(f.size, blob)) return false;
        bool nested = !audio && (f.type == DLCManager::e_DLCType_TexturePack || f.type == DLCManager::e_DLCType_Audio);
        if (!nested || !widen(blob, f.size, f.type == DLCManager::e_DLCType_Audio, f.data)) {
            f.data.assign(blob, blob + f.size);
        }
    }

    put32(out, fileCount);
    for (const File &f : files) {
        put32(out, static_cast<unsigned int>(f.data.size()));
        put32(out, f.type);
        put32(out, static_cast<unsigned int>(f.name.size()));
        putString(out, f.name, 12, sizeof(C4JStorage::DLC_FILE_DETAILS));
    }
    for (const File &f : files) {
        put32(out, static_cast<unsigned int>(f.params.size()));
        writeParams(out, f.params);
        out.insert(out.end(), f.data.begin(), f.data.end());
    }
    return true;
}

} // namespace

PBYTE mcle_dlc_widen_pck(PBYTE pbData, DWORD &dwLength) {
    if (pbData == nullptr) return pbData;
    std::vector<BYTE> out;
    if (!widen(pbData, dwLength, false, out)) {
        app.DebugPrintf("DLC data file couldn't be read, left as it is\n");
        return pbData;
    }
    PBYTE widened = new BYTE[out.size()];
    memcpy(widened, out.data(), out.size());
    delete[] pbData;
    dwLength = static_cast<DWORD>(out.size());
    return widened;
}
