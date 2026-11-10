// app.GetString(IDS_*) for the iOS menu code. Same source as upstream:
// CMinecraftApp::loadStringTable reads languages.loc out of the media
// archive; here it sits loose in Documents next to the SWFs.

#include "MCLEStrings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <unordered_map>
#include <sys/utsname.h>

extern const char *ios_documents_dir(void);

namespace {

std::mutex g_mu;
StringTable *g_table = nullptr;
bool g_tried = false;
std::unordered_map<int, std::string> g_cache;
std::unordered_map<int, std::wstring> g_named;

struct Model { const char *id; const char *name; };
const Model kModels[] = {
    {"iPhone8,1", "iPhone 6s"}, {"iPhone8,2", "iPhone 6s Plus"}, {"iPhone8,4", "iPhone SE"},
    {"iPhone9,1", "iPhone 7"}, {"iPhone9,3", "iPhone 7"},
    {"iPhone9,2", "iPhone 7 Plus"}, {"iPhone9,4", "iPhone 7 Plus"},
    {"iPhone10,1", "iPhone 8"}, {"iPhone10,4", "iPhone 8"},
    {"iPhone10,2", "iPhone 8 Plus"}, {"iPhone10,5", "iPhone 8 Plus"},
    {"iPhone10,3", "iPhone X"}, {"iPhone10,6", "iPhone X"},
    {"iPhone11,2", "iPhone XS"}, {"iPhone11,4", "iPhone XS Max"}, {"iPhone11,6", "iPhone XS Max"},
    {"iPhone11,8", "iPhone XR"},
    {"iPhone12,1", "iPhone 11"}, {"iPhone12,3", "iPhone 11 Pro"}, {"iPhone12,5", "iPhone 11 Pro Max"},
    {"iPhone12,8", "iPhone SE (2nd generation)"},
    {"iPhone13,1", "iPhone 12 mini"}, {"iPhone13,2", "iPhone 12"},
    {"iPhone13,3", "iPhone 12 Pro"}, {"iPhone13,4", "iPhone 12 Pro Max"},
    {"iPhone14,4", "iPhone 13 mini"}, {"iPhone14,5", "iPhone 13"},
    {"iPhone14,2", "iPhone 13 Pro"}, {"iPhone14,3", "iPhone 13 Pro Max"},
    {"iPhone14,6", "iPhone SE (3rd generation)"},
    {"iPhone14,7", "iPhone 14"}, {"iPhone14,8", "iPhone 14 Plus"},
    {"iPhone15,2", "iPhone 14 Pro"}, {"iPhone15,3", "iPhone 14 Pro Max"},
    {"iPhone15,4", "iPhone 15"}, {"iPhone15,5", "iPhone 15 Plus"},
    {"iPhone16,1", "iPhone 15 Pro"}, {"iPhone16,2", "iPhone 15 Pro Max"},
    {"iPhone17,3", "iPhone 16"}, {"iPhone17,4", "iPhone 16 Plus"},
    {"iPhone17,1", "iPhone 16 Pro"}, {"iPhone17,2", "iPhone 16 Pro Max"},
    {"iPhone17,5", "iPhone 16e"},
    {"iPhone18,3", "iPhone 17"}, {"iPhone18,4", "iPhone Air"},
    {"iPhone18,1", "iPhone 17 Pro"}, {"iPhone18,2", "iPhone 17 Pro Max"},
    {"iPod9,1", "iPod touch"},
    {"iPad5,1", "iPad mini 4"}, {"iPad5,2", "iPad mini 4"},
    {"iPad5,3", "iPad Air 2"}, {"iPad5,4", "iPad Air 2"},
    {"iPad6,3", "iPad Pro (9.7-inch)"}, {"iPad6,4", "iPad Pro (9.7-inch)"},
    {"iPad6,7", "iPad Pro (12.9-inch)"}, {"iPad6,8", "iPad Pro (12.9-inch)"},
    {"iPad6,11", "iPad (5th generation)"}, {"iPad6,12", "iPad (5th generation)"},
    {"iPad7,1", "iPad Pro (12.9-inch) (2nd generation)"}, {"iPad7,2", "iPad Pro (12.9-inch) (2nd generation)"},
    {"iPad7,3", "iPad Pro (10.5-inch)"}, {"iPad7,4", "iPad Pro (10.5-inch)"},
    {"iPad7,5", "iPad (6th generation)"}, {"iPad7,6", "iPad (6th generation)"},
    {"iPad7,11", "iPad (7th generation)"}, {"iPad7,12", "iPad (7th generation)"},
    {"iPad8,1", "iPad Pro (11-inch)"}, {"iPad8,2", "iPad Pro (11-inch)"},
    {"iPad8,3", "iPad Pro (11-inch)"}, {"iPad8,4", "iPad Pro (11-inch)"},
    {"iPad8,5", "iPad Pro (12.9-inch) (3rd generation)"}, {"iPad8,6", "iPad Pro (12.9-inch) (3rd generation)"},
    {"iPad8,7", "iPad Pro (12.9-inch) (3rd generation)"}, {"iPad8,8", "iPad Pro (12.9-inch) (3rd generation)"},
    {"iPad8,9", "iPad Pro (11-inch) (2nd generation)"}, {"iPad8,10", "iPad Pro (11-inch) (2nd generation)"},
    {"iPad8,11", "iPad Pro (12.9-inch) (4th generation)"}, {"iPad8,12", "iPad Pro (12.9-inch) (4th generation)"},
    {"iPad11,1", "iPad mini (5th generation)"}, {"iPad11,2", "iPad mini (5th generation)"},
    {"iPad11,3", "iPad Air (3rd generation)"}, {"iPad11,4", "iPad Air (3rd generation)"},
    {"iPad11,6", "iPad (8th generation)"}, {"iPad11,7", "iPad (8th generation)"},
    {"iPad12,1", "iPad (9th generation)"}, {"iPad12,2", "iPad (9th generation)"},
    {"iPad13,1", "iPad Air (4th generation)"}, {"iPad13,2", "iPad Air (4th generation)"},
    {"iPad13,4", "iPad Pro (11-inch) (3rd generation)"}, {"iPad13,5", "iPad Pro (11-inch) (3rd generation)"},
    {"iPad13,6", "iPad Pro (11-inch) (3rd generation)"}, {"iPad13,7", "iPad Pro (11-inch) (3rd generation)"},
    {"iPad13,8", "iPad Pro (12.9-inch) (5th generation)"}, {"iPad13,9", "iPad Pro (12.9-inch) (5th generation)"},
    {"iPad13,10", "iPad Pro (12.9-inch) (5th generation)"}, {"iPad13,11", "iPad Pro (12.9-inch) (5th generation)"},
    {"iPad13,16", "iPad Air (5th generation)"}, {"iPad13,17", "iPad Air (5th generation)"},
    {"iPad13,18", "iPad (10th generation)"}, {"iPad13,19", "iPad (10th generation)"},
    {"iPad14,1", "iPad mini (6th generation)"}, {"iPad14,2", "iPad mini (6th generation)"},
    {"iPad14,3", "iPad Pro (11-inch) (4th generation)"}, {"iPad14,4", "iPad Pro (11-inch) (4th generation)"},
    {"iPad14,5", "iPad Pro (12.9-inch) (6th generation)"}, {"iPad14,6", "iPad Pro (12.9-inch) (6th generation)"},
    {"iPad14,8", "iPad Air 11-inch (M2)"}, {"iPad14,9", "iPad Air 11-inch (M2)"},
    {"iPad14,10", "iPad Air 13-inch (M2)"}, {"iPad14,11", "iPad Air 13-inch (M2)"},
    {"iPad15,3", "iPad Air 11-inch (M3)"}, {"iPad15,4", "iPad Air 11-inch (M3)"},
    {"iPad15,5", "iPad Air 13-inch (M3)"}, {"iPad15,6", "iPad Air 13-inch (M3)"},
    {"iPad15,7", "iPad (A16)"}, {"iPad15,8", "iPad (A16)"},
    {"iPad16,1", "iPad mini (A17 Pro)"}, {"iPad16,2", "iPad mini (A17 Pro)"},
    {"iPad16,3", "iPad Pro 11-inch (M4)"}, {"iPad16,4", "iPad Pro 11-inch (M4)"},
    {"iPad16,5", "iPad Pro 13-inch (M4)"}, {"iPad16,6", "iPad Pro 13-inch (M4)"},
};

// What the device is called, for the strings that name the Xbox One
std::wstring device_name() {
    struct utsname u;
    std::string id = uname(&u) == 0 ? u.machine : "";
    if (const char *sim = getenv("SIMULATOR_MODEL_IDENTIFIER")) id = sim;
    std::string name;
    for (const Model &m : kModels)
        if (id == m.id) name = m.name;
    if (name.empty()) {
        if (id.rfind("iPad", 0) == 0) name = "iPad";
        else if (id.rfind("iPod", 0) == 0) name = "iPod touch";
        else name = "iPhone";
    }
    return std::wstring(name.begin(), name.end());
}

void replace_all(std::wstring &s, const std::wstring &from, const std::wstring &to) {
    for (size_t p = s.find(from); p != std::wstring::npos; p = s.find(from, p + to.size()))
        s.replace(p, from.size(), to);
}

// The device in place of the console: "your Xbox One console" and the
// platform name. "Minecraft: Xbox One Edition" is the game's name, left as is.
const wchar_t *named_locked(int id) {
    const wchar_t *s = g_table ? g_table->getString(id) : nullptr;
    if (!s) return L"";
    auto it = g_named.find(id);
    if (it != g_named.end()) return it->second.c_str();
    if (!wcsstr(s, L"Xbox One")) return s;
    static const std::wstring name = device_name();
    std::wstring out = s;
    if (out == L"Xbox One") out = name;
    replace_all(out, L"Xbox One console", name);
    return g_named.emplace(id, out).first->second.c_str();
}

void load_locked() {
    if (g_tried) return;
    g_tried = true;
    const char *root = ios_documents_dir();
    if (!root || !*root) return;
    std::string path = std::string(root) + "/languages.loc";
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0) {
        // StringTable keeps a pointer to the blob, so it stays alive
        unsigned char *data = new unsigned char[(size_t)n];
        if (fread(data, 1, (size_t)n, f) == (size_t)n)
            g_table = new StringTable(data, (DWORD)n);
        else
            delete[] data;
    }
    fclose(f);
}

std::string to_utf8(const wchar_t *w) {
    std::string s;
    for (; w && *w; ++w) {
        unsigned int c = (unsigned int)*w;
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

} // namespace

// CMinecraftApp::GetString. The table's strings live as long as it does.
// Windows64_Minecraft.cpp's g_Win64Username: the first line of username.txt
// beside the game's files, "Player" without one
extern "C" const wchar_t *mcle_username(void) {
    static std::wstring name;
    static std::once_flag once;
    std::call_once(once, [] {
        char buf[128] = {};
        const char *root = ios_documents_dir();
        const std::string path = std::string(root ? root : "") + "/username.txt";
        if (FILE *f = fopen(path.c_str(), "r")) {
            if (fgets(buf, sizeof(buf), f)) {
                int len = static_cast<int>(strlen(buf));
                while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
                    buf[--len] = '\0';
                }
            }
            fclose(f);
        }
        // g_Win64Username holds 16 characters
        buf[16] = '\0';
        if (buf[0] == 0) strcpy(buf, "Player");
        name.assign(buf, buf + strlen(buf));
    });
    return name.c_str();
}

extern "C" const wchar_t *mcle_wstring(int id) {
    std::lock_guard<std::mutex> lk(g_mu);
    load_locked();
    return named_locked(id);
}

extern "C" const char *mcle_string(int id) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_cache.find(id);
    if (it != g_cache.end()) return it->second.c_str();
    load_locked();
    std::string s;
    if (g_table) s = to_utf8(named_locked(id));
    return g_cache.emplace(id, s).first->second.c_str();
}
