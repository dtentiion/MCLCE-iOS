// app.GetString(IDS_*) for the iOS menu code. Same source as upstream:
// CMinecraftApp::loadStringTable reads languages.loc out of the media
// archive; here it sits loose in Documents next to the SWFs.

#include "MCLEStrings.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

extern const char *ios_documents_dir(void);

namespace {

std::mutex g_mu;
StringTable *g_table = nullptr;
bool g_tried = false;
std::unordered_map<int, std::string> g_cache;

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

extern "C" const char *mcle_string(int id) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_cache.find(id);
    if (it != g_cache.end()) return it->second.c_str();
    load_locked();
    std::string s;
    if (g_table) s = to_utf8(g_table->getString(id));
    return g_cache.emplace(id, s).first->second.c_str();
}
