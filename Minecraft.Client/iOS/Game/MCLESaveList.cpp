// Save list for LoadOrJoin and LoadMenu.
//
// Windows64 parity:
//   - UIScene_LoadOrJoinMenu::tick sorts the GameHDD folders newest first
//     (folder names are YYYYMMDDHHMMSS) and labels each one with
//     ReadLevelNameFromSaveFile, falling back to the save title.
//   - UIScene_LoadMenu::tick pulls seed / host options / texture pack out
//     of the thumbnail's 4J tEXt chunks (CMinecraftApp::GetImageTextData).
//
// On iOS the GameHDD equivalent is Documents/saves/<folder>/<name>.ms.

#include "../../../upstream/Minecraft.World/Compression.h"
#include "../../../upstream/Minecraft.World/NbtIo.h"
#include "../../../upstream/Minecraft.World/CompoundTag.h"

#include "MCLESaveList.h"

#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern const char *ios_documents_dir(void);

namespace {

struct SaveEntry {
    std::string folder;
    std::string msPath;
    std::string name;
    std::string seed;
    std::string thumbPath;
    long long size = 0;
    unsigned int hostOptions = 0;
    bool hostOptionsRead = false;
    unsigned int texturePack = 0;
};

std::vector<SaveEntry> g_saves;

bool read_file(const std::string &path, std::vector<unsigned char> &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

std::string to_utf8(const std::wstring &w) {
    std::string s;
    for (wchar_t wc : w) {
        unsigned int c = (unsigned int)wc;
        if (c < 0x80) {
            s += (char)c;
        } else if (c < 0x800) {
            s += (char)(0xC0 | (c >> 6));
            s += (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            s += (char)(0xE0 | (c >> 12));
            s += (char)(0x80 | ((c >> 6) & 0x3F));
            s += (char)(0x80 | (c & 0x3F));
        } else {
            s += (char)(0xF0 | (c >> 18));
            s += (char)(0x80 | ((c >> 12) & 0x3F));
            s += (char)(0x80 | ((c >> 6) & 0x3F));
            s += (char)(0x80 | (c & 0x3F));
        }
    }
    return s;
}

unsigned int le32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

unsigned int be32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

// Port of ReadLevelNameFromSaveFile (UIScene_LoadOrJoinMenu.cpp:32).
// File table entries are read at the 144-byte disk stride with UTF-16
// names, since FileEntrySaveData is wider in memory on iOS.
std::string read_level_name(const std::string &folderPath, const std::string &msPath) {
    FILE *fr = fopen((folderPath + "/worldname.txt").c_str(), "r");
    if (fr) {
        char buf[128] = {};
        if (fgets(buf, sizeof(buf), fr)) {
            int len = (int)strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' '))
                buf[--len] = '\0';
            fclose(fr);
            if (len > 0) return std::string(buf);
        } else {
            fclose(fr);
        }
    }

    std::vector<unsigned char> raw;
    if (!read_file(msPath, raw) || raw.size() < 12) return "";

    // Compression is a per-thread singleton in this build
    thread_local bool s_compressionReady = false;
    if (!s_compressionReady) {
        Compression::CreateNewThreadStorage();
        s_compressionReady = true;
    }

    std::vector<unsigned char> decomp;
    const unsigned char *save = raw.data();
    unsigned int saveSize = (unsigned int)raw.size();
    if (le32(raw.data()) == 0) {
        unsigned int decompSize = le32(raw.data() + 4);
        if (decompSize == 0 || decompSize > 128 * 1024 * 1024) return "";
        decomp.resize(decompSize);
        Compression::getCompression()->Decompress(decomp.data(), &decompSize,
                                                  raw.data() + 8, (unsigned int)raw.size() - 8);
        save = decomp.data();
        saveSize = decompSize;
    }

    std::wstring result;
    if (saveSize >= 12) {
        const unsigned int kEntrySize = 144;
        unsigned int headerOffset = le32(save);
        unsigned int numEntries = le32(save + 4);
        if (headerOffset < saveSize && numEntries > 0 && numEntries < 10000 &&
            (unsigned long long)headerOffset + (unsigned long long)numEntries * kEntrySize <= saveSize) {
            static const char kLevelDat[] = "level.dat";
            for (unsigned int i = 0; i < numEntries; i++) {
                const unsigned char *e = save + headerOffset + i * kEntrySize;
                bool match = true;
                for (int j = 0; j < 64; j++) {
                    unsigned int c = (unsigned int)e[j * 2] | ((unsigned int)e[j * 2 + 1] << 8);
                    unsigned int want = j < 9 ? (unsigned char)kLevelDat[j] : 0;
                    if (c != want) { match = false; break; }
                    if (c == 0) break;
                }
                if (!match) continue;
                unsigned int len = le32(e + 128);
                unsigned int off = le32(e + 132);
                if (off >= 12 && (unsigned long long)off + len <= saveSize && len > 0 && len < 4 * 1024 * 1024) {
                    byteArray ba;
                    ba.data = (byte *)(save + off);
                    ba.length = len;
                    CompoundTag *root = NbtIo::decompress(ba);
                    if (root != nullptr) {
                        CompoundTag *dataTag = root->getCompound(L"Data");
                        if (dataTag != nullptr) result = dataTag->getString(L"LevelName");
                        delete root;
                    }
                }
                break;
            }
        }
    }

    // "world" is the engine default, same fallback as upstream
    if (result == L"world") result.clear();
    return to_utf8(result);
}

// Port of CMinecraftApp::GetImageTextData (Consoles_App.cpp:8347).
void read_thumbnail_text(const std::vector<unsigned char> &png, SaveEntry &out) {
    static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (png.size() < 8 || memcmp(png.data(), kSig, 8) != 0) return;
    const unsigned char *p = png.data();
    size_t n = png.size();
    size_t pos = 8;
    while (pos + 8 <= n) {
        unsigned int chunkLen = be32(p + pos);
        unsigned int chunkType = be32(p + pos + 4);
        pos += 8;
        if (pos + chunkLen > n) break;
        if (chunkType == 0x74455874) { // tEXt
            const unsigned char *kw = p + pos;
            const unsigned char *end = p + pos + chunkLen;
            while (kw < end) {
                std::string key;
                while (kw < end && *kw) key += (char)*kw++;
                kw++;
                std::string val;
                while (kw < end && *kw) val += (char)*kw++;
                if (key == "4J_SEED") {
                    out.seed = val;
                } else if (key == "4J_HOSTOPTIONS") {
                    out.hostOptionsRead = true;
                    out.hostOptions = (unsigned int)strtoul(val.substr(0, 8).c_str(), nullptr, 16);
                } else if (key == "4J_TEXTUREPACK") {
                    out.texturePack = (unsigned int)strtoul(val.substr(0, 8).c_str(), nullptr, 16);
                }
            }
        }
        pos += chunkLen + 4; // data + CRC
    }
}

} // namespace

extern "C" int mcle_saves_refresh(void) {
    g_saves.clear();
    const char *root = ios_documents_dir();
    if (!root || !*root) return 0;
    std::string savesDir = std::string(root) + "/saves";

    DIR *d = opendir(savesDir.c_str());
    if (!d) return 0;
    while (struct dirent *de = readdir(d)) {
        if (de->d_name[0] == '.') continue;
        std::string folderPath = savesDir + "/" + de->d_name;
        struct stat st;
        if (stat(folderPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        // Win64 names the bundle after the world, so take the first .ms
        std::string msName;
        if (DIR *sd = opendir(folderPath.c_str())) {
            while (struct dirent *fe = readdir(sd)) {
                size_t len = strlen(fe->d_name);
                if (len > 3 && strcmp(fe->d_name + len - 3, ".ms") == 0) {
                    msName = fe->d_name;
                    break;
                }
            }
            closedir(sd);
        }
        if (msName.empty()) continue;

        SaveEntry e;
        e.folder = de->d_name;
        e.msPath = folderPath + "/" + msName;
        e.name = read_level_name(folderPath, e.msPath);
        if (e.name.empty()) e.name = msName.substr(0, msName.size() - 3);

        struct stat ms;
        if (stat(e.msPath.c_str(), &ms) == 0) e.size = (long long)ms.st_size;

        std::vector<unsigned char> thumb;
        if (read_file(folderPath + "/thumbnails/thumbData.png", thumb)) {
            e.thumbPath = folderPath + "/thumbnails/thumbData.png";
            read_thumbnail_text(thumb, e);
        }

        g_saves.push_back(e);
    }
    closedir(d);

    std::sort(g_saves.begin(), g_saves.end(), [](const SaveEntry &a, const SaveEntry &b) {
        return strcmp(a.folder.c_str(), b.folder.c_str()) > 0;
    });
    return (int)g_saves.size();
}

extern "C" int mcle_saves_count(void) { return (int)g_saves.size(); }

static const SaveEntry *entry(int i) {
    return (i >= 0 && i < (int)g_saves.size()) ? &g_saves[(size_t)i] : nullptr;
}

extern "C" const char *mcle_saves_folder(int i) { auto e = entry(i); return e ? e->folder.c_str() : ""; }
extern "C" const char *mcle_saves_name(int i)   { auto e = entry(i); return e ? e->name.c_str() : ""; }
extern "C" const char *mcle_saves_seed(int i)   { auto e = entry(i); return e ? e->seed.c_str() : ""; }

extern "C" const char *mcle_saves_thumbnail(int i) { auto e = entry(i); return e ? e->thumbPath.c_str() : ""; }
extern "C" long long mcle_saves_size(int i)        { auto e = entry(i); return e ? e->size : 0; }

extern "C" int mcle_saves_host_options(int i, unsigned int *out) {
    auto e = entry(i);
    if (!e || !e->hostOptionsRead) return 0;
    if (out) *out = e->hostOptions;
    return 1;
}
