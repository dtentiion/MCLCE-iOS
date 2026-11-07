// UIScene_EndPoem's text: the constructor puts the three win strings
// together, formats them, fills in the player's name and the noise, and
// splits the result into the paragraphs the movie asks for one at a time.
// The scene side is in MinecraftViewController.mm.

#include <cstring>
#include <string>
#include <vector>

#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/Font.h"
#include "../../../upstream/Minecraft.Client/Common/Colours/ColourTable.h"
#include "../../../upstream/Minecraft.World/Random.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/StringHelpers.h"

std::wstring mcle_format_html_string(int iPad, const std::wstring &desc, int shadowColour);   // MCLEHowToPlay.cpp
int mcle_html_colour(eMinecraftColour colour);   // MCLEHowToPlay.cpp

namespace {

wstring noNoiseString;
wstring noiseString;
vector<int> m_noiseLengths;
vector<wstring> m_paragraphs;
std::string g_utf8;

void updateNoise() {
    Minecraft *pMinecraft = Minecraft::GetInstance();
    noiseString = noNoiseString;

    int length = 0;
    wchar_t replacements[64];
    wstring replaceString = L"";
    wchar_t randomChar = L'a';
    Random *random = pMinecraft->font->random;

    wstring tag = L"{*NOISE*}";

    auto it = m_noiseLengths.begin();
    size_t found = noiseString.find(tag);
    while (found != string::npos && it != m_noiseLengths.end()) {
        length = *it;
        ++it;

        replaceString = L"";
        for (int i = 0; i < length; ++i) {
            // ui.UsingBitmapFont(), the Latin font
            randomChar = SharedConstants::acceptableLetters[random->nextInt(static_cast<int>(SharedConstants::acceptableLetters.length()))];

            wstring randomCharStr = L"";
            randomCharStr.push_back(randomChar);
            if (randomChar == L'<') {
                randomCharStr = L"&lt;";
            } else if (randomChar == L'>') {
                randomCharStr = L"&gt;";
            } else if (randomChar == L'"') {
                randomCharStr = L"&quot;";
            } else if (randomChar == L'&') {
                randomCharStr = L"&amp;";
            } else if (randomChar == L'\\') {
                randomCharStr = L"\\\\";
            } else if (randomChar == L'{') {
                randomCharStr = L"}";
            }

            int randomVal = random->nextInt(2);
            eMinecraftColour colour = eHTMLColor_8;
            if (randomVal == 1) colour = eHTMLColor_9;
            else if (randomVal == 2) colour = eHTMLColor_a;
            memset(replacements, 0, 64 * sizeof(wchar_t));
            swprintf(replacements, 64, L"<font color=\"#%08x\" shadowcolor=\"#80000000\">%ls</font>",
                     mcle_html_colour(colour), randomCharStr.c_str());
            replaceString.append(replacements);
        }

        noiseString.replace(found, tag.length(), replaceString);

        found = noiseString.find(tag, found + 1);
    }
}

std::string toUtf8(const std::wstring &w) {
    std::string s;
    for (wchar_t wc : w) {
        unsigned int c = static_cast<unsigned int>(wc);
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

} // namespace

// UIScene_EndPoem's constructor
extern "C" void mcle_end_poem_init(void) {
    noNoiseString.clear();
    m_noiseLengths.clear();

    noNoiseString.append(app.GetString(IDS_WIN_TEXT));
    noNoiseString.append(app.GetString(IDS_WIN_TEXT_PART_2));
    noNoiseString.append(app.GetString(IDS_WIN_TEXT_PART_3));

    // 4J Stu - Iggy seems to strip our trailing linebreaks, so added a space to made sure it scrolls this far
    noNoiseString.append(L" ");

    noNoiseString = mcle_format_html_string(0, noNoiseString, 0xff000000);

    // The client player's display name is the profile's
    wstring playerName = L"";
    playerName = escapeXML(ProfileManager.GetDisplayName(0));
    noNoiseString = replaceAll(noNoiseString, L"{*PLAYER*}", playerName);

    Random random(8124371);
    size_t found = noNoiseString.find(L"{*NOISE*}");
    int length;
    while (found != string::npos) {
        length = random.nextInt(4) + 3;
        m_noiseLengths.push_back(length);
        found = noNoiseString.find(L"{*NOISE*}", found + 1);
    }

    updateNoise();

    // 4J-JEV: Find paragraph start and end points.
    m_paragraphs = vector<wstring>();
    int lastIndex = 0;
    for (int index = 0; index != wstring::npos; index = noiseString.find(L"<br /><br />", index + 12, 12)) {
        m_paragraphs.push_back(noiseString.substr(lastIndex, index - lastIndex));
        lastIndex = index;
    }
    m_paragraphs.push_back(noiseString.substr(lastIndex, noiseString.length() - lastIndex));
}

extern "C" int mcle_end_poem_paragraph_count(void) {
    return static_cast<int>(m_paragraphs.size());
}

// A paragraph as UTF-8, until the next call
extern "C" const char *mcle_end_poem_paragraph(int index) {
    if (index < 0 || index >= static_cast<int>(m_paragraphs.size())) return "";
    g_utf8 = toUtf8(m_paragraphs[index]);
    return g_utf8.c_str();
}

// UIScene_EndPoem::handleInput's up and down on both sticks:
// sendInputToMovie gives the movie a key down while one is held and a key
// up when it's let go, and the movie scrolls back or speeds up meanwhile.
// The d-pad's keys already reach the movie through Ruffle's gamepad
// mapping, so they're left out here.
extern "C" void mcle_end_poem_input(unsigned int buttons, unsigned int prevButtons,
                                    void (*sendKey)(int iggyKeyCode, int down)) {
    static const struct {
        int action;
        int iggyKeyCode;
    } keys[] = {
        {ACTION_MENU_DOWN, 40},              // IGGY_KEYCODE_DOWN
        {ACTION_MENU_UP, 38},                // IGGY_KEYCODE_UP
        {ACTION_MENU_OTHER_STICK_DOWN, 123}, // IGGY_KEYCODE_F12
        {ACTION_MENU_OTHER_STICK_UP, 122},   // IGGY_KEYCODE_F11
    };
    const unsigned int dpad = _360_JOY_BUTTON_DPAD_UP | _360_JOY_BUTTON_DPAD_DOWN;
    for (const auto &k : keys) {
        unsigned int mapped = InputManager.GetGameJoypadMaps(InputManager.GetJoypadMapVal(0), k.action) & ~dpad;
        bool down = (buttons & mapped) != 0;
        if (down != ((prevButtons & mapped) != 0)) sendKey(k.iggyKeyCode, down ? 1 : 0);
    }
}
