// Save list for LoadOrJoin and LoadMenu.
//
// As on Windows64:
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
#include <unistd.h>
#include <zlib.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
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

// C4JStorage::AllocateSaveData / SaveSaveData for the loaded world. Like
// Windows64 the whole buffer goes out, the header in front says how much of
// it the save is.
namespace {
std::mutex g_storageMutex;
std::string g_saveFolder;
// SetSaveTitle, what a new save's .ms is called
std::string g_saveTitle;
unsigned char *g_saveBuffer = nullptr;
unsigned int g_saveBufferSize = 0;
// SetSaveImages: the thumbnail and the text that goes in its png
std::vector<unsigned char> g_saveThumb;
std::vector<unsigned char> g_saveText;

void put_be32(std::vector<unsigned char> &out, unsigned int v) {
    out.push_back((unsigned char)(v >> 24));
    out.push_back((unsigned char)(v >> 16));
    out.push_back((unsigned char)(v >> 8));
    out.push_back((unsigned char)v);
}

void png_chunk(std::vector<unsigned char> &out, const char *type, const unsigned char *data, unsigned int len) {
    put_be32(out, len);
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (len) out.insert(out.end(), data, data + len);
    put_be32(out, (unsigned int)crc32(0, &out[start], (uInt)(len + 4)));
}

// The save's thumbnails/thumbData.png, with SetSaveImages' text in a tEXt
// chunk after the header, which is where LoadMenu reads the seed, the host
// options and the texture pack back from. A save with no capture (the one
// made when a world is created, or on going to the background) keeps the
// image already there, and a new one gets the texture pack's icon.
bool write_thumbnail(const std::string &root, const std::string &dir) {
    static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const std::string path = dir + "/thumbnails/thumbData.png";
    std::vector<unsigned char> png = g_saveThumb;
    if (png.empty()) read_file(path, png);
    if (png.empty()) read_file(root + "/Graphics/TexturePackIcon.png", png);
    if (png.size() < 8 || memcmp(png.data(), kSig, 8) != 0) return false;

    std::vector<unsigned char> out(png.begin(), png.begin() + 8);
    size_t pos = 8;
    while (pos + 12 <= png.size()) {
        unsigned int len = be32(&png[pos]);
        unsigned int type = be32(&png[pos + 4]);
        if (pos + 12 + len > png.size()) break;
        // The old text goes, the new text follows the header
        if (type != 0x74455874 || g_saveText.empty()) {
            out.insert(out.end(), png.begin() + pos, png.begin() + pos + 12 + len);
        }
        if (type == 0x49484452 && !g_saveText.empty()) { // IHDR
            std::vector<unsigned char> chunk = {'t', 'E', 'X', 't'};
            chunk.insert(chunk.end(), g_saveText.begin(), g_saveText.end());
            put_be32(out, (unsigned int)g_saveText.size());
            out.insert(out.end(), chunk.begin(), chunk.end());
            put_be32(out, (unsigned int)crc32(0, chunk.data(), (uInt)chunk.size()));
        }
        pos += 12 + len;
    }

    mkdir((dir + "/thumbnails").c_str(), 0755);
    const std::string temp = path + ".tmp";
    FILE *f = fopen(temp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(temp.c_str(), path.c_str()) == 0;
    if (!ok) remove(temp.c_str());
    return ok;
}

// A new save's folder is named by the time it's made, like the GameHDD ones
std::string new_save_folder(const std::string &savesDir) {
    time_t now = time(nullptr);
    for (;;) {
        char name[32];
        struct tm local;
        localtime_r(&now, &local);
        strftime(name, sizeof(name), "%Y%m%d%H%M%S", &local);
        struct stat st;
        if (stat((savesDir + "/" + name).c_str(), &st) != 0) return name;
        ++now;
    }
}
}

extern "C" void mcle_storage_set_save_folder(const char *folder) {
    std::lock_guard<std::mutex> lock(g_storageMutex);
    g_saveFolder = folder ? folder : "";
}

// C4JStorage::ResetSaveData: the next save is a new one
extern "C" void mcle_storage_reset_save_data(void) {
    std::lock_guard<std::mutex> lock(g_storageMutex);
    g_saveFolder.clear();
    g_saveTitle.clear();
}

extern "C" void mcle_storage_set_save_title(const wchar_t *title) {
    std::lock_guard<std::mutex> lock(g_storageMutex);
    g_saveTitle = to_utf8(title ? title : L"");
    // Not a path, the title is the file's name
    std::replace(g_saveTitle.begin(), g_saveTitle.end(), '/', '_');
}

extern "C" void *mcle_storage_allocate_save_data(unsigned int size) {
    std::lock_guard<std::mutex> lock(g_storageMutex);
    free(g_saveBuffer);
    g_saveBuffer = static_cast<unsigned char *>(malloc(size));
    g_saveBufferSize = g_saveBuffer ? size : 0;
    return g_saveBuffer;
}

extern "C" int mcle_storage_save_save_data(void) {
    std::lock_guard<std::mutex> lock(g_storageMutex);
    const char *root = ios_documents_dir();
    if (!g_saveBuffer || !root) return 0;
    std::string savesDir = std::string(root) + "/saves";
    if (g_saveFolder.empty() && !g_saveTitle.empty()) g_saveFolder = new_save_folder(savesDir);
    if (g_saveFolder.empty()) return 0;
    // The world's .ms, named after the world, the first one in its folder
    std::string dir = savesDir + "/" + g_saveFolder;
    std::string target;
    if (DIR *d = opendir(dir.c_str())) {
        while (struct dirent *e = readdir(d)) {
            std::string name = e->d_name;
            if (name.size() > 3 && name.compare(name.size() - 3, 3, ".ms") == 0) {
                target = dir + "/" + name;
                break;
            }
        }
        closedir(d);
    }
    if (target.empty()) {
        // A save that isn't on disk yet goes in its own folder
        if (g_saveTitle.empty()) return 0;
        mkdir(savesDir.c_str(), 0755);
        mkdir(dir.c_str(), 0755);
        target = dir + "/" + g_saveTitle + ".ms";
    }
    // Written next to it and swapped in, so a kill mid write leaves the old one
    std::string temp = target + ".tmp";
    FILE *f = fopen(temp.c_str(), "wb");
    if (!f) return 0;
    bool ok = fwrite(g_saveBuffer, 1, g_saveBufferSize, f) == g_saveBufferSize;
    ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(temp.c_str(), target.c_str()) == 0;
    if (!ok) remove(temp.c_str());
    if (ok) write_thumbnail(root, dir);
    free(g_saveBuffer);
    g_saveBuffer = nullptr;
    g_saveBufferSize = 0;
    return ok ? 1 : 0;
}

extern "C" void mcle_storage_set_save_images(const unsigned char *thumb, unsigned int thumbBytes,
                                             const unsigned char *text, unsigned int textBytes) {
    std::lock_guard<std::mutex> lock(g_storageMutex);
    g_saveThumb.assign(thumb, thumb ? thumb + thumbBytes : thumb);
    g_saveText.assign(text, text ? text + textBytes : text);
}

extern "C" void mcle_metal_capture_thumbnail(void);
extern "C" int mcle_metal_take_thumbnail(unsigned char *rgba);

// CConsoleMinecraftApp::CaptureSaveThumbnail: the next world frame is taken
extern "C" void mcle_capture_save_thumbnail(void) {
    mcle_metal_capture_thumbnail();
}

// GetSaveThumbnail (Windows64_App.cpp): the capture as a 64x64 RGBA png
// with an sRGB chunk like RenderManager.CaptureThumbnail makes, handed over
// once. Nothing if no capture happened, like the first save of a new world.
// Null arguments just let go of it.
extern "C" void mcle_get_save_thumbnail(unsigned char **data, unsigned int *size) {
    if (data) *data = nullptr;
    if (size) *size = 0;
    const unsigned int kSize = 64;
    std::vector<unsigned char> rgba(kSize * kSize * 4);
    if (!mcle_metal_take_thumbnail(rgba.data()) || !data) return;

    std::vector<unsigned char> raw;
    raw.reserve(kSize * (kSize * 4 + 1));
    for (unsigned int y = 0; y < kSize; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + y * kSize * 4, rgba.begin() + (y + 1) * kSize * 4);
    }
    uLongf zlen = compressBound((uLong)raw.size());
    std::vector<unsigned char> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), Z_BEST_COMPRESSION) != Z_OK) return;

    static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<unsigned char> png(kSig, kSig + 8);
    std::vector<unsigned char> ihdr;
    put_be32(ihdr, kSize);
    put_be32(ihdr, kSize);
    const unsigned char rest[5] = {8, 6, 0, 0, 0}; // 8 bit RGBA
    ihdr.insert(ihdr.end(), rest, rest + 5);
    png_chunk(png, "IHDR", ihdr.data(), (unsigned int)ihdr.size());
    const unsigned char intent = 0;
    png_chunk(png, "sRGB", &intent, 1);
    png_chunk(png, "IDAT", z.data(), (unsigned int)zlen);
    png_chunk(png, "IEND", nullptr, 0);

    *data = new unsigned char[png.size()];
    memcpy(*data, png.data(), png.size());
    if (size) *size = (unsigned int)png.size();
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

extern "C" unsigned int mcle_saves_texture_pack(int i) {
    auto e = entry(i);
    return e ? e->texturePack : 0;
}

// Win64_DeleteSaveDirectory (UIScene_LoadOrJoinMenu.cpp:2391): what's in the
// folder, then the folder. The names are read first so none get skipped
// while they go, and a link is removed rather than followed.
static bool delete_save_directory(const std::string &path) {
    std::vector<std::string> children;
    if (DIR *d = opendir(path.c_str())) {
        while (struct dirent *e = readdir(d)) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            children.push_back(path + "/" + e->d_name);
        }
        closedir(d);
    }
    for (const std::string &child : children) {
        struct stat st;
        if (lstat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) delete_save_directory(child);
        else unlink(child.c_str());
    }
    return rmdir(path.c_str()) == 0;
}

// A save's own folder under saves/, never saves/ itself or anything outside it
static bool is_save_folder(const std::string &folder) {
    return !folder.empty() && folder != "." && folder != ".." && folder.find('/') == std::string::npos;
}

// DeleteSaveDialogReturned on Windows64 deletes GameHDD\<save filename>, here
// saves/<folder>
extern "C" int mcle_saves_delete(int i) {
    std::lock_guard<std::mutex> lock(g_storageMutex);
    auto e = entry(i);
    const char *root = ios_documents_dir();
    if (!e || !root || !*root || !is_save_folder(e->folder)) return 0;
    return delete_save_directory(std::string(root) + "/saves/" + e->folder) ? 1 : 0;
}

// KeyboardCompleteWorldNameCallback on Windows64 writes the new name to
// GameHDD\<save filename>\worldname.txt, which read_level_name takes over the
// level's own name. It's UTF-8 here, the way that reads it back.
extern "C" int mcle_saves_rename(int i, const char *name) {
    std::lock_guard<std::mutex> lock(g_storageMutex);
    const char *root = ios_documents_dir();
    if (i < 0 || i >= (int)g_saves.size() || !name || !root || !*root) return 0;
    SaveEntry &e = g_saves[(size_t)i];
    if (!is_save_folder(e.folder)) return 0;
    bool written = false;
    if (FILE *fw = fopen((std::string(root) + "/saves/" + e.folder + "/worldname.txt").c_str(), "w")) {
        written = fputs(name, fw) >= 0;
        written = (fclose(fw) == 0) && written;
    }
    // Update the in-memory display name so the list reflects it immediately
    e.name = std::string(name).substr(0, 127);
    return written ? 1 : 0;
}
