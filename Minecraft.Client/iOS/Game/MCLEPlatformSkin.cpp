// The PS4 platform skin for a PlayStation pad, with the parts that aren't
// buttons put back from the Windows64 skin. skinHDOrbis.swf carries the
// PS4 button glyphs but also its own title logo ("PlayStation 4 Edition"),
// credits background and so on; Windows64 always shows skinHDWin's.
#include "MCLEPlatformSkin.h"
#include <cstring>
#include <string>
#include <unordered_map>
#include <zlib.h>

namespace {

struct Tag {
    unsigned code;
    size_t start; // header
    size_t body;
    size_t len;
};

struct Swf {
    std::vector<unsigned char> d;
    size_t first = 0;
    std::vector<Tag> tags;
    std::unordered_map<unsigned, size_t> chars; // char id -> tag index
    std::unordered_map<std::string, unsigned> classes;
};

unsigned u16(const std::vector<unsigned char>& d, size_t p) { return d[p] | (d[p + 1] << 8); }
unsigned u32(const std::vector<unsigned char>& d, size_t p) {
    return d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((unsigned)d[p + 3] << 24);
}

bool isBitmap(unsigned code) {
    return code == 6 || code == 20 || code == 21 || code == 35 || code == 36 || code == 90;
}

bool isDefinition(unsigned code) {
    switch (code) {
    case 2: case 6: case 7: case 10: case 11: case 20: case 21: case 22: case 32:
    case 33: case 34: case 35: case 36: case 37: case 39: case 46: case 48:
    case 62: case 75: case 83: case 84: case 90: case 91:
        return true;
    }
    return false;
}

bool parse(const std::vector<unsigned char>& in, Swf& s) {
    if (in.size() < 8) return false;
    if (in[0] == 'F' && in[1] == 'W' && in[2] == 'S') {
        s.d = in;
    } else if (in[0] == 'C' && in[1] == 'W' && in[2] == 'S') {
        uLongf size = u32(in, 4);
        if (size < 8) return false;
        s.d.assign(in.begin(), in.begin() + 8);
        s.d.resize(size);
        uLongf left = size - 8;
        if (uncompress(s.d.data() + 8, &left, in.data() + 8, in.size() - 8) != Z_OK) return false;
        s.d.resize(8 + left);
    } else {
        return false;
    }
    const std::vector<unsigned char>& d = s.d;
    if (d.size() < 9) return false;
    size_t rectBytes = (5 + 4 * (d[8] >> 3) + 7) / 8;
    size_t pos = 8 + rectBytes + 4;
    while (pos + 2 <= d.size()) {
        Tag t;
        t.start = pos;
        unsigned h = u16(d, pos);
        pos += 2;
        t.code = h >> 6;
        t.len = h & 63;
        if (t.len == 63) {
            if (pos + 4 > d.size()) return false;
            t.len = u32(d, pos);
            pos += 4;
        }
        t.body = pos;
        if (pos + t.len > d.size()) return false;
        pos += t.len;
        if (isDefinition(t.code) && t.len >= 2) s.chars[u16(d, t.body)] = s.tags.size();
        if (t.code == 76 && t.len >= 2) { // SymbolClass
            unsigned n = u16(d, t.body);
            size_t q = t.body + 2, end = t.body + t.len;
            for (unsigned i = 0; i < n && q + 2 < end; ++i) {
                unsigned id = u16(d, q);
                q += 2;
                size_t e = q;
                while (e < end && d[e]) ++e;
                s.classes[std::string((const char*)&d[q], e - q)] = id;
                q = e + 1;
            }
        }
        s.tags.push_back(t);
        if (t.code == 0) break;
    }
    s.first = 8 + rectBytes + 4;
    return true;
}

// The bitmap tag behind a class that is just a bitmap
const Tag* classBitmap(const Swf& s, const char* name) {
    auto c = s.classes.find(name);
    if (c == s.classes.end()) return nullptr;
    auto t = s.chars.find(c->second);
    if (t == s.chars.end()) return nullptr;
    const Tag& tag = s.tags[t->second];
    return isBitmap(tag.code) ? &tag : nullptr;
}

void putTag(std::vector<unsigned char>& out, unsigned code, const unsigned char* body, size_t len) {
    unsigned h = (code << 6) | 63;
    out.push_back(h & 0xFF);
    out.push_back(h >> 8);
    for (int i = 0; i < 4; ++i) out.push_back((len >> (8 * i)) & 0xFF);
    out.insert(out.end(), body, body + len);
}

} // namespace

bool mcle_merge_platform_skin(const std::vector<unsigned char>& orbis,
                              const std::vector<unsigned char>& win,
                              std::vector<unsigned char>& out) {
    Swf o, w;
    if (!parse(orbis, o) || !parse(win, w)) return false;
    // Logo is a sprite around MenuTitle in both, so MenuTitle covers it
    static const char* kFromWin[] = {
        "MenuTitle", "MenuTitleSmall", "CreditBackground", "Panorama_Background_N",
        "Panorama_Background_S", "DefaultPack_Comparison", "TutorialExitScreenshot",
    };
    std::unordered_map<size_t, const Tag*> replace; // orbis tag index -> win tag
    for (const char* name : kFromWin) {
        const Tag* from = classBitmap(w, name);
        const Tag* to = classBitmap(o, name);
        // DefineBits needs the file's own JPEGTables, so it can't move
        if (!from || !to || from->code == 6) continue;
        replace[to - o.tags.data()] = from;
    }
    if (replace.empty()) return false;

    std::vector<unsigned char> body(o.d.begin(), o.d.begin() + o.first);
    for (size_t i = 0; i < o.tags.size(); ++i) {
        const Tag& t = o.tags[i];
        auto r = replace.find(i);
        if (r == replace.end()) {
            body.insert(body.end(), o.d.begin() + t.start, o.d.begin() + t.body + t.len);
            continue;
        }
        // Windows64's bitmap under the PS4 skin's character id
        const Tag& from = *r->second;
        std::vector<unsigned char> b(w.d.begin() + from.body, w.d.begin() + from.body + from.len);
        b[0] = o.d[t.body];
        b[1] = o.d[t.body + 1];
        putTag(body, from.code, b.data(), b.size());
    }

    uLongf packed = compressBound(body.size() - 8);
    out.assign(body.begin(), body.begin() + 8);
    out.resize(8 + packed);
    if (compress2(out.data() + 8, &packed, body.data() + 8, body.size() - 8, Z_DEFAULT_COMPRESSION) != Z_OK)
        return false;
    out.resize(8 + packed);
    out[0] = 'C';
    out[1] = 'W';
    out[2] = 'S';
    const size_t size = body.size();
    for (int i = 0; i < 4; ++i) out[4 + i] = (size >> (8 * i)) & 0xFF;
    return true;
}
