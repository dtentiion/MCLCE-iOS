// C4JStorage's installed DLC for iOS. Windows64 keeps each pack in its own
// folder under Windows64Media/DLC; the phone's Documents mirrors that
// install, so the packs are the folders in Documents/Windows64Media/DLC.
// Mounting a pack puts its folder on a drive name ("TPACK"), the way the
// Windows64 storage maps them, and GetMountedPath turns "TPACK:\Data\..."
// into a path in that folder.

#include <dirent.h>
#include <dispatch/dispatch.h>
#include <sys/stat.h>

#include <algorithm>
#include <functional>
#include <mutex>
#include <string>
#include <strings.h>
#include <vector>

namespace {

struct InstalledPack {
    XCONTENT_DATA data;
    std::string folder;
};

// A drive name and the pack folder that's on it
struct DriveMapping {
    std::string mount;
    std::string folder;
};

// The drive a mount with no name goes on
const char *const kPackageRoot = "DLCDrive";

std::vector<InstalledPack> g_installed;
// File paths are looked up on any thread
std::mutex g_mountLock;
std::vector<DriveMapping> g_mappings;
// The pack mounted last, the one GetMountedDLCFileList lists
std::string g_currentMapped;

std::string dlcRoot() {
    const char *docs = StorageManager.GetSaveRootPath();
    return std::string(docs ? docs : "") + "/Windows64Media/DLC";
}

bool isDirectory(const std::string &path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string &path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Names in a folder, in the order NTFS lists them (case doesn't count)
std::vector<std::string> listFolder(const std::string &path) {
    std::vector<std::string> names;
    DIR *dir = opendir(path.c_str());
    if (!dir) return names;
    while (dirent *entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == ".." || name[0] == '.') continue;
        names.push_back(name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end(),
              [](const std::string &a, const std::string &b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    return names;
}

// UTF-8 folder name to the display name
void widen(const std::string &in, WCHAR *out, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < in.size() && n + 1 < cap;) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        unsigned int cp;
        int len;
        if (c < 0x80) {
            cp = c;
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            len = 3;
        } else {
            cp = c & 0x07;
            len = 4;
        }
        for (int k = 1; k < len && i + k < in.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(in[i + k]) & 0x3F);
        }
        i += len;
        out[n++] = static_cast<WCHAR>(cp);
    }
    out[n] = 0;
}

// The callback later, from the main run loop
void later(std::function<void()> fn) {
    auto *call = new std::function<void()>(std::move(fn));
    dispatch_async_f(dispatch_get_main_queue(), call, [](void *context) {
        auto *f = static_cast<std::function<void()> *>(context);
        (*f)();
        delete f;
    });
}

} // namespace

C4JStorage::EDLCStatus C4JStorage::GetInstalledDLC(int iPad, int (*Func)(LPVOID, int, int), LPVOID lpParam) {
    g_installed.clear();
    const std::string root = dlcRoot();
    for (const std::string &name : listFolder(root)) {
        if (!isDirectory(root + "/" + name)) continue;
        InstalledPack pack = {};
        pack.data.DeviceID = 0;
        pack.data.dwContentType = 0;
        widen(name, pack.data.szDisplayName, XCONTENT_MAX_DISPLAYNAME_LENGTH);
        strncpy(reinterpret_cast<char *>(pack.data.szFileName), name.c_str(), XCONTENT_MAX_FILENAME_LENGTH - 1);
        pack.folder = root + "/" + name;
        g_installed.push_back(pack);
    }
    const int count = static_cast<int>(g_installed.size());
    if (Func) later([Func, lpParam, count, iPad]() { Func(lpParam, count, iPad); });
    return count > 0 ? EDLC_Pending : EDLC_NoInstalledDLC;
}

XCONTENT_DATA &C4JStorage::GetDLC(DWORD dw) {
    static XCONTENT_DATA s_none = {};
    return dw < g_installed.size() ? g_installed[dw].data : s_none;
}

DWORD C4JStorage::MountInstalledDLC(int iPad, DWORD dwDLC, int (*Func)(LPVOID, int, DWORD, DWORD), LPVOID lpParam,
                                    LPCSTR szMountDrive) {
    if (dwDLC >= g_installed.size()) return ERROR_FILE_NOT_FOUND;
    {
        std::lock_guard<std::mutex> lock(g_mountLock);
        const std::string mount = szMountDrive ? szMountDrive : kPackageRoot;
        // A drive mounted again is on the new pack
        g_mappings.erase(std::remove_if(g_mappings.begin(), g_mappings.end(),
                                        [&](const DriveMapping &m) { return m.mount == mount; }),
                         g_mappings.end());
        g_mappings.push_back({mount, g_installed[dwDLC].folder});
        g_currentMapped = g_installed[dwDLC].folder;
    }
    // Every pack counts as owned
    if (Func) later([Func, lpParam, iPad]() { Func(lpParam, iPad, ERROR_SUCCESS, 1); });
    return ERROR_IO_PENDING;
}

DWORD C4JStorage::UnmountInstalledDLC(LPCSTR szMountDrive) {
    std::lock_guard<std::mutex> lock(g_mountLock);
    const std::string mount = szMountDrive ? szMountDrive : kPackageRoot;
    g_mappings.erase(std::remove_if(g_mappings.begin(), g_mappings.end(),
                                    [&](const DriveMapping &m) { return m.mount == mount; }),
                     g_mappings.end());
    return ERROR_SUCCESS;
}

// The .pck files at the top of the mounted pack's folder
void C4JStorage::GetMountedDLCFileList(const char *szMountDrive, std::vector<std::string> &fileList) {
    fileList.clear();
    std::string folder;
    {
        std::lock_guard<std::mutex> lock(g_mountLock);
        folder = g_currentMapped;
    }
    if (folder.empty()) return;
    for (const std::string &name : listFolder(folder)) {
        const std::string path = folder + "/" + name;
        if (name.size() < 4 || strcasecmp(name.c_str() + name.size() - 4, ".pck") != 0) continue;
        if (isFile(path)) fileList.push_back(path);
    }
}

// A path on a mounted drive ("TPACK:\Data\media.arc") in that drive's
// folder. Anything else isn't on a mount and comes back empty.
std::string C4JStorage::GetMountedPath(std::string szMount) {
    for (size_t ch = 0; ch < szMount.size(); ++ch) {
        if (szMount[ch] == '/' || szMount[ch] == '\\') return std::string();
        if (szMount[ch] != ':') continue;

        const std::string driveName = szMount.substr(0, ch);
        std::lock_guard<std::mutex> lock(g_mountLock);
        for (const DriveMapping &mapping : g_mappings) {
            if (mapping.mount != driveName) continue;
            std::string rest = szMount.substr(ch + 1);
            std::replace(rest.begin(), rest.end(), '\\', '/');
            while (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
            return mapping.folder + "/" + rest;
        }
        break;
    }
    return std::string();
}
