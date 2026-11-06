// The texture packs on iOS: the default pack (the media in Documents), the
// repository Minecraft::init makes, and CMinecraftApp's texture pack side
// for the app stub (Common/Consoles_App.cpp).

#include "MCLETexturePacks.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../../upstream/Minecraft.World/File.h"
#include "../../../upstream/Minecraft.Client/ArchiveFile.h"
#include "../../../upstream/Minecraft.Client/BufferedImage.h"
#include "../../../upstream/Minecraft.Client/Textures.h"
#include "../../../upstream/Minecraft.Client/TexturePackRepository.h"
#include "../../../upstream/Minecraft.Client/FolderTexturePack.h"

namespace {

// The media archive is unpacked into Documents on iOS, the same files
// CMinecraftApp's m_mediaArchive holds on Windows64
std::string mediaPath(const std::wstring &filename) {
    const char *root = StorageManager.GetSaveRootPath();
    std::string path = std::string(root ? root : "") + "/";
    for (wchar_t c : filename) path += (c == L'\\') ? '/' : static_cast<char>(c);
    return path;
}

bool readWhole(const std::string &path, std::vector<unsigned char> &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = n > 0;
    if (ok) {
        out.resize(static_cast<size_t>(n));
        ok = fread(out.data(), 1, static_cast<size_t>(n), f) == static_cast<size_t>(n);
    }
    fclose(f);
    return ok;
}

// The default pack. It's DefaultTexturePack on the consoles, reading the
// game's own media. Here that media is in Documents, which the File and
// BufferedImage paths don't reach the way they do from the Windows64
// working folder, so this keeps the reads the port had working and
// otherwise does what DefaultTexturePack does.
class IosFolderTexturePack : public FolderTexturePack {
public:
    IosFolderTexturePack(DWORD id, File *folder) : FolderTexturePack(id, L"Minecraft", folder, nullptr) {
        // 4J Stu - These calls need to be in the most derived version of the class
        loadIcon();
        loadName();
        loadDescription();
    }

    bool hasData() override { return true; }
    bool isLoadingData() override { return false; }
    DLCPack *getDLCPack() override { return nullptr; }

    std::wstring getDesc1() override { return app.GetString(IDS_DEFAULT_TEXTUREPACK); }

    // Images are found by the BufferedImage load path, so every file counts
    // as there
    InputStream *getResource(const std::wstring &, bool) override { return nullptr; }
    void unload(Textures *) override {}
    void load(Textures *) override {}
    bool hasFile(const std::wstring &name, bool) override { return hasFile(name); }
    bool hasFile(const std::wstring &) override { return true; }
    std::wstring getAnimationString(const std::wstring &, const std::wstring &, bool) override { return L""; }
    std::wstring getAnimationString(const std::wstring &, const std::wstring &) override { return L""; }

    // FolderTexturePack::getPath is "Common\\" + the folder's absolute path.
    // BufferedImage puts this in front of "res/..." and the load path
    // resolves it against Documents.
    std::wstring getPath(bool = false, const char * = nullptr) override { return L"Common/"; }

    BufferedImage *getImageResource(const std::wstring &filename, bool filenameHasExtension, bool bTitleUpdateTexture,
                                    const std::wstring &drive) override {
        // BufferedImage builds drive + L"res" + filename, so the name needs
        // its leading slash
        std::wstring path = (filename.empty() || filename[0] == L'/') ? filename : (L"/" + filename);
        return new BufferedImage(path, filenameHasExtension, bTitleUpdateTexture, drive);
    }

    // AbstractTexturePack::loadColourTable reads these through getPath(true),
    // which is relative here, so read them straight out of Documents
    void loadColourTable() override {
        const char *root = StorageManager.GetSaveRootPath();
        if (!root || !*root) return;
        std::string base(root);
        std::vector<unsigned char> col;
        if (readWhole(base + "/Common/res/TitleUpdate/res/colours.col", col)) {
            delete m_colourTable;
            m_colourTable = new ColourTable(col.data(), static_cast<DWORD>(col.size()));
        }
        std::vector<unsigned char> html;
        if (m_colourTable && readWhole(base + "/HTMLColours.col", html)) {
            m_colourTable->loadColoursFromData(html.data(), static_cast<DWORD>(html.size()));
        }
    }

    // DefaultTexturePack's
    void loadUI() override {
        loadDefaultUI();

        AbstractTexturePack::loadUI();
    }

    void unloadUI() override { AbstractTexturePack::unloadUI(); }

    std::wstring getXuiRootPath() override { return L""; }

protected:
    // DefaultTexturePack's
    void loadIcon() override {
        if (app.hasArchiveFile(L"Graphics\\TexturePackIcon.png")) {
            byteArray ba = app.getArchiveFile(L"Graphics\\TexturePackIcon.png");
            m_iconData = ba.data;
            m_iconSize = ba.length;
        }
    }

    void loadDescription() override { desc1 = L"LOCALISE ME: The default look of Minecraft"; }

    void loadName() override { texname = L"Minecraft"; }
};

} // namespace

void mcle_texture_packs_init(Minecraft *minecraft) {
    const char *root = StorageManager.GetSaveRootPath();
    std::string commonDir = std::string(root ? root : "") + "/Common";
    std::wstring wCommonDir(commonDir.begin(), commonDir.end());
    TexturePackRepository::DEFAULT_TEXTURE_PACK =
        new IosFolderTexturePack(TexturePackRepository::DEFAULT_TEXTURE_PACK_ID, new File(wCommonDir));

    // workingDirectory = File(L"")
    minecraft->skins = new TexturePackRepository(File(L""), minecraft);
    minecraft->skins->addDebugPacks();
    app.DebugPrintf("texture packs: %u, default at %p\n", minecraft->skins->getTexturePackCount(),
                    (void *)TexturePackRepository::DEFAULT_TEXTURE_PACK);
}

// UIController::ReloadSkin. The menus' skin movies are the ones in
// Documents, a pack's own skin isn't swapped in yet.
void mcle_ui_reload_skin() {}

void McleAppStub::SetAction(int iPad, eXuiAction action, LPVOID param) {
    if ((m_eXuiAction[iPad] == eAppAction_ReloadTexturePack) && (action == eAppAction_EthernetDisconnected)) {
        app.DebugPrintf("Invalid change of App action for pad %d from %d to %d, ignoring\n", iPad, m_eXuiAction[iPad],
                        action);
    } else if ((m_eXuiAction[iPad] == eAppAction_ReloadTexturePack) && (action == eAppAction_ExitWorld)) {
        app.DebugPrintf("Invalid change of App action for pad %d from %d to %d, ignoring\n", iPad, m_eXuiAction[iPad],
                        action);
    } else if (m_eXuiAction[iPad] == eAppAction_ExitWorldCapturedThumbnail && action != eAppAction_Idle) {
        app.DebugPrintf("Invalid change of App action for pad %d from %d to %d, ignoring\n", iPad, m_eXuiAction[iPad],
                        action);
    } else {
        app.DebugPrintf("Changing App action for pad %d from %d to %d\n", iPad, m_eXuiAction[iPad], action);
        m_eXuiAction[iPad] = action;
        m_eXuiActionParam[iPad] = param;
    }
}

// The app actions. The world's own ones (save, exit) are run by the game
// loop on iOS, this has the texture pack reload.
void McleAppStub::HandleXuiActions() {
    for (int i = 0; i < XUSER_MAX_COUNT; i++) {
        eXuiAction eAction = app.GetXuiAction(i);
        if (eAction == eAppAction_Idle) continue;

        switch (eAction) {
        case eAppAction_ReloadTexturePack: {
            SetAction(i, eAppAction_Idle);
            Minecraft *pMinecraft = Minecraft::GetInstance();
            // The textures are made with the first world here. Until then
            // there's nothing to reload, and they're made from the selected
            // pack.
            if (pMinecraft->textures) pMinecraft->textures->reloadAll();
            pMinecraft->skins->updateUI();

            // 4J-PB  - If the texture pack has audio, we need to switch to this
            if (pMinecraft->skins->getSelected()->hasAudio()) {
                Minecraft::GetInstance()->soundEngine->playStreaming(L"", 0, 0, 0, 1, 1);
            }
        } break;

        default:
            break;
        }
    }
}

std::wstring McleAppStub::getFilePath(DWORD packId, std::wstring filename, bool bAddDataFolder,
                                      std::wstring mountPoint) {
    std::wstring path = getRootPath(packId, true, bAddDataFolder, mountPoint) + filename;
    File f(path);
    if (f.exists()) {
        return path;
    }
    return getRootPath(packId, false, true, mountPoint) + filename;
}

namespace {

enum ETitleUpdateTexturePacks {
    eTUTP_Halloween = 0x804,
};

// The Windows64 one, the port keeps its install layout
const std::wstring titleUpdateTexturePackRoot = L"Windows64\\DLC\\";

} // namespace

std::wstring McleAppStub::getRootPath(DWORD packId, bool allowOverride, bool bAddDataFolder,
                                      std::wstring mountPoint) {
    std::wstring path = mountPoint;
    if (allowOverride) {
        switch (packId) {
        case eTUTP_Halloween:
            path = titleUpdateTexturePackRoot + L"Halloween Texture Pack";
            break;
        };
        File folder(path);
        if (!folder.exists()) {
            path = mountPoint;
        }
    }

    if (bAddDataFolder) {
        return path + L"\\Data\\";
    } else {
        return path + L"\\";
    }
}

int McleAppStub::getArchiveFileSize(const std::wstring &filename) {
    TexturePack *tPack = nullptr;
    Minecraft *pMinecraft = Minecraft::GetInstance();
    if (pMinecraft && pMinecraft->skins) tPack = pMinecraft->skins->getSelected();
    if (tPack && tPack->hasData() && tPack->getArchiveFile() && tPack->getArchiveFile()->hasFile(filename)) {
        return tPack->getArchiveFile()->getFileSize(filename);
    }
    File media(convStringToWstring(mediaPath(filename)));
    return media.exists() ? static_cast<int>(media.length()) : -1;
}

bool McleAppStub::hasArchiveFile(const std::wstring &filename) {
    TexturePack *tPack = nullptr;
    Minecraft *pMinecraft = Minecraft::GetInstance();
    if (pMinecraft && pMinecraft->skins) tPack = pMinecraft->skins->getSelected();
    if (tPack && tPack->hasData() && tPack->getArchiveFile() && tPack->getArchiveFile()->hasFile(filename)) return true;
    return File(convStringToWstring(mediaPath(filename))).exists();
}

byteArray McleAppStub::getArchiveFile(const std::wstring &filename) {
    TexturePack *tPack = nullptr;
    Minecraft *pMinecraft = Minecraft::GetInstance();
    if (pMinecraft && pMinecraft->skins) tPack = pMinecraft->skins->getSelected();
    if (tPack && tPack->hasData() && tPack->getArchiveFile() && tPack->getArchiveFile()->hasFile(filename)) {
        return tPack->getArchiveFile()->getFile(filename);
    }
    std::vector<unsigned char> bytes;
    if (!readWhole(mediaPath(filename), bytes)) return byteArray();
    byteArray out(static_cast<unsigned int>(bytes.size()), false);
    memcpy(out.data, bytes.data(), bytes.size());
    return out;
}
