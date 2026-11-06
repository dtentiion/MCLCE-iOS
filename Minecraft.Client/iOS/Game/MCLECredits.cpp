// UIScene_Credits' text. The table is upstream's own gs_aCreditDefs, copied
// out of UIScene_Credits.cpp by scripts/gen-credits.py, and each entry is
// formatted the way the scene's constructor and tick do it.

#include <cstdio>
#include <cstring>
#include <string>

#include "../../../upstream/Minecraft.Client/Common/UI/UIEnums.h"

extern "C" const char *mcle_string(int id); // MCLEStrings.cpp

namespace {

#define CREDIT_ICON -2

// UIScene_Credits::ECreditIcons
enum ECreditIcons {
    eCreditIcon_Iggy,
    eCreditIcon_Miles,
    eCreditIcon_Dolby,
};

// UIStructs.h SCreditTextItemDef
struct CreditDef {
    const wchar_t *m_Text;
    int m_iStringID[2];
    ECreditTextTypes m_eType;
};

// MAX_CREDIT_STRINGS as Windows64 has it, the entries past the table's end
// left empty
const int kMaxCreditStrings = (80 + 318) + 8;

const CreditDef kCreditDefs[kMaxCreditStrings] = {
#include "../../../upstream/Minecraft.Client/iOS_CreditDefs.inc"
};

std::string toUtf8(const wchar_t *w) {
    std::string s;
    for (; w && *w; ++w) {
        unsigned int c = static_cast<unsigned int>(*w);
        if (c < 0x80) {
            s += static_cast<char>(c);
        } else if (c < 0x800) {
            s += static_cast<char>(0xC0 | (c >> 6));
            s += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            s += static_cast<char>(0xE0 | ((c >> 12) & 0x0F));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return s;
}

// swprintf(creditsString, 128, m_Text, app.GetString(id0)[, app.GetString(id1)])
std::string format(const CreditDef &def) {
    std::string fmt = toUtf8(def.m_Text);
    if (def.m_iStringID[0] == NO_TRANSLATED_STRING) return fmt;
    const int ids[2] = {def.m_iStringID[0], def.m_iStringID[1]};
    std::string out;
    int arg = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt.compare(i, 3, "%ls") == 0 && arg < 2 && ids[arg] != NO_TRANSLATED_STRING) {
            const char *str = mcle_string(ids[arg++]);
            out += str ? str : "";
            i += 2;
        } else {
            out += fmt[i];
        }
    }
    return out;
}

} // namespace

// m_iNumTextDefs: MAX_CREDIT_STRINGS plus the DLC credits, of which there are none
extern "C" int mcle_credits_count(void) { return kMaxCreditStrings; }

// One credit: 0 for an empty slot (tick skips those), 1 for a label with its
// text and ECreditTextTypes, 2 for an image with its ECreditIcons in *type
extern "C" int mcle_credits_get(int index, char *out, int cap, int *type) {
    if (cap > 0) out[0] = 0;
    if (index < 0 || index >= kMaxCreditStrings) return 0;
    const CreditDef &def = kCreditDefs[index];
    if (def.m_Text == nullptr) return 0;
    if (def.m_iStringID[0] == CREDIT_ICON) {
        *type = def.m_iStringID[1];
        return 2;
    }
    std::string text = format(def);
    if (cap > 0) snprintf(out, cap, "%s", text.c_str());
    *type = static_cast<int>(def.m_eType);
    return 1;
}
