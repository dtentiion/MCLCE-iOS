// How to Play: UIScene_HowToPlay's pages and labels, and CMinecraftApp's
// FormatHTMLString that their text goes through. The tables and function
// bodies are upstream's, copied out by scripts/gen-howtoplay.py and
// scripts/gen-html-format.py. The scene side is in MinecraftViewController.mm.

#include <string>

#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/TexturePackRepository.h"
#include "../../../upstream/Minecraft.Client/TexturePack.h"
#include "../../../upstream/Minecraft.Client/Common/Colours/ColourTable.h"
#include "../../../upstream/Minecraft.Client/Common/XUI/XUI_XZP_Icons.h"
#include "../../../upstream/Minecraft.World/StringHelpers.h"

// XInput's VK_PAD codes, which GetVKReplacement switches on
#ifndef VK_PAD_A
#define VK_PAD_A                0x5800
#define VK_PAD_B                0x5801
#define VK_PAD_X                0x5802
#define VK_PAD_Y                0x5803
#define VK_PAD_RSHOULDER        0x5804
#define VK_PAD_LSHOULDER        0x5805
#define VK_PAD_LTRIGGER         0x5806
#define VK_PAD_RTRIGGER         0x5807
#define VK_PAD_DPAD_UP          0x5810
#define VK_PAD_DPAD_DOWN        0x5811
#define VK_PAD_DPAD_LEFT        0x5812
#define VK_PAD_DPAD_RIGHT       0x5813
#define VK_PAD_START            0x5814
#define VK_PAD_BACK             0x5815
#define VK_PAD_LTHUMB_PRESS     0x5816
#define VK_PAD_RTHUMB_PRESS     0x5817
#define VK_PAD_LTHUMB_UP        0x5820
#define VK_PAD_LTHUMB_DOWN      0x5821
#define VK_PAD_LTHUMB_RIGHT     0x5822
#define VK_PAD_LTHUMB_LEFT      0x5823
#define VK_PAD_LTHUMB_UPLEFT    0x5824
#define VK_PAD_LTHUMB_UPRIGHT   0x5825
#define VK_PAD_LTHUMB_DOWNRIGHT 0x5826
#define VK_PAD_LTHUMB_DOWNLEFT  0x5827
#define VK_PAD_RTHUMB_UP        0x5830
#define VK_PAD_RTHUMB_DOWN      0x5831
#define VK_PAD_RTHUMB_RIGHT     0x5832
#define VK_PAD_RTHUMB_LEFT      0x5833
#define VK_PAD_RTHUMB_UPLEFT    0x5834
#define VK_PAD_RTHUMB_UPRIGHT   0x5835
#define VK_PAD_RTHUMB_DOWNRIGHT 0x5836
#define VK_PAD_RTHUMB_DOWNLEFT  0x5837
#endif

// The CMinecraftApp members the formatting needs
class MCLEAppText {
public:
    int GetHTMLColour(eMinecraftColour colour) {
        Minecraft *pMinecraft = Minecraft::GetInstance();
        return pMinecraft->skins->getSelected()->getColourTable()->getColour(colour);
    }
    wstring FormatHTMLString(int iPad, const wstring &desc, int shadowColour = 0xFFFFFFFF);
    wstring GetActionReplacement(int iPad, unsigned char ucAction);
    wstring GetVKReplacement(unsigned int uiVKey);
    wstring GetIconReplacement(unsigned int uiIcon);
};

#include "../../../upstream/Minecraft.Client/iOS_FormatHTML.inc"

namespace {

// UIControl_Label's text, all the constructor gives them
struct MCLEHowToPlayLabel {
    std::wstring text;
    void init(const std::wstring &s) { text = s; }
    void init(const wchar_t *s) { text = s ? s : L""; }
};

} // namespace

#include "../../../upstream/Minecraft.Client/iOS_HowToPlay.inc"

namespace {

MCLEAppText g_appText;
std::string g_pageText;
MCLEHowToPlayLabel g_labels[UIScene_HowToPlay::eHowToPlay_NumLabels];
std::string g_labelText;
bool g_labelsDone = false;

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

extern "C" int mcle_htp_num_pages(void) { return eHowToPlay_NumPages; }

// UIScene_HowToPlayMenu::m_uiHTPSceneA
extern "C" int mcle_htp_menu_page(int button) {
    const int count = static_cast<int>(sizeof(gs_uiHTPSceneA) / sizeof(gs_uiHTPSceneA[0]));
    return (button >= 0 && button < count) ? static_cast<int>(gs_uiHTPSceneA[button]) : 0;
}

// gs_pageToFlashMapping, LoadHowToPlayPage's page
extern "C" int mcle_htp_flash_page(int page) {
    return (page >= 0 && page < eHowToPlay_NumPages) ? gs_pageToFlashMapping[page] : 0;
}

// UIScene_HowToPlay::StartPage's text for a page
extern "C" const char *mcle_htp_page_text(int page) {
    if (page < 0 || page >= eHowToPlay_NumPages) return "";
    const UIScene_HowToPlay::SHowToPlayPageDef *pDef = &gs_aPageDefs[page];

    // Replace button identifiers in the text with actual button images
    wstring replacedText = g_appText.FormatHTMLString(0, app.GetString(pDef->m_iTextStringID));
    replacedText = replaceAll(replacedText, L"{*KICK_PLAYER_DESCRIPTION*}", app.GetString(IDS_KICK_PLAYER_DESCRIPTION));
    replacedText = replaceAll(replacedText, L"{*BACK_BUTTON*}", app.GetString(IDS_BACK_BUTTON));
    replacedText = replaceAll(replacedText, L"{*DISABLES_ACHIEVEMENTS*}", app.GetString(IDS_HOST_OPTION_DISABLES_ACHIEVEMENTS));

    // strip out any tab characters and repeated spaces
    stripWhitespaceForHtml(replacedText, true);

    // Set the text colour
    wchar_t startTags[64];
    swprintf(startTags, 64, L"<font color=\"#%08x\">", g_appText.GetHTMLColour(eHTMLColor_White));
    g_pageText = toUtf8(startTags + replacedText);
    return g_pageText.c_str();
}

// The page's labels: m_iLabelStartIndex and m_iLabelCount
extern "C" void mcle_htp_page_labels(int page, int *start, int *count) {
    *start = 0;
    *count = 0;
    if (page < 0 || page >= eHowToPlay_NumPages) return;
    *start = gs_aPageDefs[page].m_iLabelStartIndex;
    *count = gs_aPageDefs[page].m_iLabelCount;
}

// The scene's labels, each with its SWF name and the constructor's text
extern "C" int mcle_htp_label_count(void) {
    return static_cast<int>(sizeof(gs_labelNames) / sizeof(gs_labelNames[0]));
}

extern "C" const char *mcle_htp_label(int i, const char **name) {
    if (!g_labelsDone) {
        UIScene_HowToPlay::initLabels(g_labels);
        g_labelsDone = true;
    }
    *name = "";
    if (i < 0 || i >= mcle_htp_label_count()) return "";
    *name = gs_labelNames[i].name;
    int label = gs_labelNames[i].label;
    g_labelText = (label >= 0 && label < UIScene_HowToPlay::eHowToPlay_NumLabels) ? toUtf8(g_labels[label].text) : "";
    return g_labelText.c_str();
}
