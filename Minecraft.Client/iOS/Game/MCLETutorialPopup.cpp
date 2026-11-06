// UIComponent_TutorialPopup. The component's logic is upstream's, its Iggy
// calls are queued as MCLEUIOps for the view controller to make on the
// TutorialPopup1080.swf sibling. The tutorial reaches it on the sim thread
// and the menus and the view controller on the main thread, so it keeps its
// own lock.

#include "MCLETutorialPopup.h"
#include "MCLEContainerScene.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "../../../upstream/Minecraft.World/net.minecraft.world.item.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.level.tile.h"
#include "../../../upstream/Minecraft.World/StringHelpers.h"
#include "../../../upstream/Minecraft.Client/Common/Tutorial/Tutorial.h"

std::wstring mcle_format_html_string(int iPad, const std::wstring &desc); // MCLEHowToPlay.cpp
std::mutex &mcle_sim_tick_mutex();                                       // MCLEGameLoop.cpp

#define TUTORIAL_POPUP_FADE_TIMER_ID 0
#define TUTORIAL_POPUP_MOVE_SCENE_TIMER_ID 1
#define TUTORIAL_POPUP_MOVE_SCENE_TIME 500

namespace {

std::recursive_mutex g_mutex;
std::vector<MCLEUIOp> g_ops;
std::vector<MCLECustomDrawRegion> g_regions;

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

MCLEUIArg argNum(double v) { return MCLEUIArg{MCLEUIArg::Number, v, std::string()}; }
MCLEUIArg argBool(bool v) { return MCLEUIArg{MCLEUIArg::Bool, v ? 1.0 : 0.0, std::string()}; }
MCLEUIArg argStr(const std::wstring &v) { return MCLEUIArg{MCLEUIArg::String, 0.0, toUtf8(v)}; }

void movieCall(const std::string &path, const char *method, std::vector<MCLEUIArg> args) {
    g_ops.push_back(MCLEUIOp{MCLEUIOp::Call, path, method, std::move(args)});
}

// UIControl::setVisible, which only sets what changed
struct PopupControl {
    std::string path;
    bool isVisible = true;

    explicit PopupControl(const char *p) : path(p) {}
    void setVisible(bool visible) {
        if (visible != isVisible) {
            g_ops.push_back(MCLEUIOp{MCLEUIOp::Visible, path, std::string(), {argBool(visible)}});
            isVisible = visible;
        }
    }
};

// UIControl_Label
struct PopupLabel : PopupControl {
    std::wstring label;
    bool labelChanged = false;

    explicit PopupLabel(const char *p) : PopupControl(p) {}
    void init(const std::wstring &text) {
        label = text;
        movieCall(path, "Init", {argStr(text)});
    }
    // UIControl_Base::setLabel
    void setLabel(const std::wstring &text, bool instant = false, bool force = false) {
        if (force || ((!label.empty() || !text.empty()) && label.compare(text) != 0)) labelChanged = true;
        label = text;
        if (labelChanged && instant) {
            labelChanged = false;
            movieCall(path, "SetLabel", {argStr(label)});
        }
    }
    // UIControl_Base::tick
    void tick() {
        if (!labelChanged) return;
        labelChanged = false;
        movieCall(path, "SetLabel", {argStr(label)});
    }
};

int64_t currentTimeMillis() {
    return System::currentTimeMillis();
}

class TutorialPopup {
public:
    // Maps to values in AS
    enum EIcons {
        e_ICON_TYPE_IGGY = 0,
        e_ICON_TYPE_ARMOUR = 1,
        e_ICON_TYPE_BREWING = 2,
        e_ICON_TYPE_DECORATION = 3,
        e_ICON_TYPE_FOOD = 4,
        e_ICON_TYPE_MATERIALS = 5,
        e_ICON_TYPE_MECHANISMS = 6,
        e_ICON_TYPE_MISC = 7,
        e_ICON_TYPE_REDSTONE_AND_TRANSPORT = 8,
        e_ICON_TYPE_STRUCTURES = 9,
        e_ICON_TYPE_TOOLS = 10,
        e_ICON_TYPE_TRANSPORT = 11,
    };

    TutorialPopup()
        : m_labelDescription("Description"), m_labelTitle("Title"), m_controlIconHolder("IconHolder"),
          m_controlExitScreenshot("ExitScreenShot") {}

    // The constructor's part once the movie is up
    void movieReady() {
        m_interactScene = nullptr;
        m_lastInteractSceneMoved = nullptr;
        m_lastSceneMovedLeft = false;
        m_bAllowFade = false;
        m_iconItem = nullptr;
        m_iconIsFoil = false;

        m_bContainerMenuVisible = false;
        m_bSplitscreenGamertagVisible = false;
        m_iconType = e_ICON_TYPE_IGGY;

        m_labelDescription.init(L"");
    }

    void SetContainerMenuVisible(bool bContainerMenuVisible) { m_bContainerMenuVisible = bContainerMenuVisible; }

    void SetTutorial(Tutorial *tutorial) { m_tutorial = tutorial; }

    void SetTutorialDescription(MCLETutorialPopupInfo *info) {
        m_interactScene = info->interactScene;
        m_messageTutorial = info->tutorial;

        wstring parsed = _SetIcon(info->icon, info->iAuxVal, info->isFoil, info->desc);
        parsed = _SetImage(parsed);
        parsed = ParseDescription(m_iPad, parsed);

        if (parsed.empty()) {
            _SetDescription(info->interactScene, L"", L"", info->allowFade, info->isReminder);
        } else {
            _SetDescription(info->interactScene, parsed, info->title, info->allowFade, info->isReminder);
        }
    }

    void RemoveInteractSceneReference(UIScene *scene) {
        if (m_interactScene == scene) {
            m_interactScene = nullptr;
        }
    }

    void SetVisible(bool visible) {
        // m_parentLayer->showComponent(0, eUIComponent_TutorialPopup, visible)
        m_shown = visible;

        if (visible && m_bAllowFade) {
            // Initialise a timer to fade us out again
            addTimer(TUTORIAL_POPUP_FADE_TIMER_ID, displayMessageTime());
        }
    }

    bool IsVisible() { return m_shown; }

    // UIScene::tick
    void tick() {
        tickTimers();
        m_labelDescription.tick();
        m_labelTitle.tick();
    }

    void customDraw(const MCLECustomDrawRegion &region) {
        if (m_iconItem != nullptr) mcle_custom_draw_slot(region, m_iconItem, 1.0f, false);
    }

    bool hasIcon() { return m_iconItem != nullptr; }

    // The component stays, but with the world gone it's emptied and the
    // tutorial it had is deleted with its game mode
    void reset() {
        MCLETutorialPopupInfo popupInfo;
        SetTutorialDescription(&popupInfo);
        m_timers.clear();
        m_interactScene = nullptr;
        m_lastInteractSceneMoved = nullptr;
        m_tutorial = nullptr;
    }

private:
    struct TimerInfo {
        bool running;
        int duration;
        int64_t targetTime;
    };

    // A scene that may be displayed behind the popup that the player is using, that will need shifted so we can see it clearly.
    UIScene *m_interactScene = nullptr, *m_lastInteractSceneMoved = nullptr;
    bool m_lastSceneMovedLeft = false;
    bool m_bAllowFade = false;
    Tutorial *m_tutorial = nullptr;
    // The tutorial a message came from, for one that comes before the
    // tutorial's first tick has handed it over with SetTutorial
    Tutorial *m_messageTutorial = nullptr;

    // m_tutorial->GetTutorialDisplayMessageTime()
    int displayMessageTime() {
        Tutorial *tutorial = m_tutorial != nullptr ? m_tutorial : m_messageTutorial;
        return tutorial != nullptr ? tutorial->GetTutorialDisplayMessageTime() : 0;
    }
    shared_ptr<ItemInstance> m_iconItem;
    bool m_iconIsFoil = false;

    bool m_bContainerMenuVisible = false;
    bool m_bSplitscreenGamertagVisible = false;

    EIcons m_iconType = e_ICON_TYPE_IGGY;

    // UILayer starts the component hidden
    bool m_shown = false;
    int m_iPad = 0;

    PopupLabel m_labelDescription, m_labelTitle;
    PopupControl m_controlIconHolder;
    PopupControl m_controlExitScreenshot;

    std::map<int, TimerInfo> m_timers;

    // UIScene::addTimer / killTimer
    void addTimer(int id, int ms) {
        int64_t currentTime = currentTimeMillis();
        TimerInfo info;
        info.running = true;
        info.duration = ms;
        info.targetTime = currentTime + ms;
        m_timers[id] = info;
    }

    void killTimer(int id) {
        auto it = m_timers.find(id);
        if (it != m_timers.end()) {
            it->second.running = false;
        }
    }

    // UIScene::tickTimers. The ones that are due are taken first, the
    // completions add and kill timers.
    void tickTimers() {
        int64_t currentTime = currentTimeMillis();
        std::vector<int> due;
        for (auto it = m_timers.begin(); it != m_timers.end();) {
            if (!it->second.running) {
                it = m_timers.erase(it);
            } else {
                if (currentTime > it->second.targetTime) due.push_back(it->first);
                ++it;
            }
        }
        for (int id : due) {
            auto it = m_timers.find(id);
            if (it == m_timers.end() || !it->second.running) continue;
            handleTimerComplete(id);

            // Auto-restart
            it = m_timers.find(id);
            if (it != m_timers.end()) it->second.targetTime = it->second.duration + currentTime;
        }
    }

    void handleTimerComplete(int id) {
        switch (id) {
        case TUTORIAL_POPUP_FADE_TIMER_ID:
            SetVisible(false);
            killTimer(id);
            addTimer(TUTORIAL_POPUP_MOVE_SCENE_TIMER_ID, TUTORIAL_POPUP_MOVE_SCENE_TIME);
            break;
        case TUTORIAL_POPUP_MOVE_SCENE_TIMER_ID:
            UpdateInteractScenePosition(IsVisible());
            killTimer(id);
            break;
        }
    }

    void _SetDescription(UIScene *interactScene, const wstring &desc, const wstring &title, bool allowFade,
                         bool isReminder) {
        m_interactScene = interactScene;
        if (interactScene != m_lastInteractSceneMoved) m_lastInteractSceneMoved = nullptr;
        if (desc.empty()) {
            SetVisible(false);
            addTimer(TUTORIAL_POPUP_MOVE_SCENE_TIMER_ID, TUTORIAL_POPUP_MOVE_SCENE_TIME);
            killTimer(TUTORIAL_POPUP_FADE_TIMER_ID);
        } else {
            SetVisible(true);
            addTimer(TUTORIAL_POPUP_MOVE_SCENE_TIMER_ID, TUTORIAL_POPUP_MOVE_SCENE_TIME);

            if (allowFade) {
                // Initialise a timer to fade us out again
                addTimer(TUTORIAL_POPUP_FADE_TIMER_ID, displayMessageTime());
            } else {
                killTimer(TUTORIAL_POPUP_FADE_TIMER_ID);
            }
            m_bAllowFade = allowFade;

            if (isReminder) {
                wstring text(app.GetString(IDS_TUTORIAL_REMINDER));
                text.append(desc);
                stripWhitespaceForHtml(text);
                // set the text colour
                wchar_t formatting[40];
                // 4J Stu - Don't set HTML font size, that's set at design time in flash
                swprintf(formatting, 40, L"<font color=\"#%08x\">", app.GetHTMLColour(eHTMLColor_White));
                text = formatting + text;

                m_labelDescription.setLabel(text, true);
            } else {
                wstring text(desc);
                stripWhitespaceForHtml(text);
                // set the text colour
                wchar_t formatting[40];
                // 4J Stu - Don't set HTML font size, that's set at design time in flash
                swprintf(formatting, 40, L"<font color=\"#%08x\">", app.GetHTMLColour(eHTMLColor_White));
                text = formatting + text;

                m_labelDescription.setLabel(text, true);
            }

            m_labelTitle.setLabel(title, true);
            m_labelTitle.setVisible(!title.empty());

            // read host setting if gamertag is visible or not and pass on to Adjust Layout function (so we can offset it to stay clear of the gamertag)
            m_bSplitscreenGamertagVisible =
                (bool)(app.GetGameSettings(ProfileManager.GetPrimaryPad(), eGameSetting_DisplaySplitscreenGamertags) != 0);
            // 4J - TomK - Offset for splitscreen gamertag?
            movieCall("", "AdjustLayout", {argBool(m_bSplitscreenGamertagVisible && !m_bContainerMenuVisible)});
        }
    }

    wstring _SetIcon(int icon, int iAuxVal, bool isFoil, LPCWSTR desc) {
        wstring temp(desc);

        bool isFixedIcon = false;

        m_iconIsFoil = isFoil;
        if (icon != TUTORIAL_NO_ICON) {
            m_iconIsFoil = false;
            m_iconItem = std::make_shared<ItemInstance>(icon, 1, iAuxVal);
        } else {
            m_iconItem = nullptr;
            wstring openTag(L"{*ICON*}");
            wstring closeTag(L"{*/ICON*}");
            size_t iconTagStartPos = temp.find(openTag);
            size_t iconStartPos = iconTagStartPos + openTag.length();
            if (iconTagStartPos > 0 && iconStartPos < temp.length()) {
                size_t iconEndPos = temp.find(closeTag, iconStartPos);

                if (iconEndPos > iconStartPos && iconEndPos < temp.length()) {
                    wstring id = temp.substr(iconStartPos, iconEndPos - iconStartPos);

                    vector<wstring> idAndAux = stringSplit(id, L':');

                    int iconId = _fromString<int>(idAndAux[0]);

                    if (idAndAux.size() > 1) {
                        iAuxVal = _fromString<int>(idAndAux[1]);
                    } else {
                        iAuxVal = 0;
                    }
                    m_iconItem = std::make_shared<ItemInstance>(iconId, 1, iAuxVal);

                    temp.replace(iconTagStartPos, iconEndPos - iconTagStartPos + closeTag.length(), L"");
                }
            }

            // remove any icon text
            else if (temp.find(L"{*CraftingTableIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Tile::workBench_Id), 1, 0);
            } else if (temp.find(L"{*SticksIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Item::stick_Id), 1, 0);
            } else if (temp.find(L"{*PlanksIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Tile::wood_Id), 1, 0);
            } else if (temp.find(L"{*WoodenShovelIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Item::shovel_wood_Id), 1, 0);
            } else if (temp.find(L"{*WoodenHatchetIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Item::hatchet_wood_Id), 1, 0);
            } else if (temp.find(L"{*WoodenPickaxeIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Item::pickAxe_wood_Id), 1, 0);
            } else if (temp.find(L"{*FurnaceIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Tile::furnace_Id), 1, 0);
            } else if (temp.find(L"{*WoodenDoorIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(Item::door_wood, 1, 0);
            } else if (temp.find(L"{*TorchIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Tile::torch_Id), 1, 0);
            } else if (temp.find(L"{*BoatIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Item::boat_Id), 1, 0);
            } else if (temp.find(L"{*FishingRodIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Item::fishingRod_Id), 1, 0);
            } else if (temp.find(L"{*FishIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Item::fish_raw_Id), 1, 0);
            } else if (temp.find(L"{*MinecartIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Item::minecart_Id), 1, 0);
            } else if (temp.find(L"{*RailIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Tile::rail_Id), 1, 0);
            } else if (temp.find(L"{*PoweredRailIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Tile::goldenRail_Id), 1, 0);
            } else if (temp.find(L"{*StructuresIcon*}") != wstring::npos) {
                isFixedIcon = true;
                setupIconHolder(e_ICON_TYPE_STRUCTURES);
            } else if (temp.find(L"{*ToolsIcon*}") != wstring::npos) {
                isFixedIcon = true;
                setupIconHolder(e_ICON_TYPE_TOOLS);
            } else if (temp.find(L"{*StoneIcon*}") != wstring::npos) {
                m_iconItem = std::make_shared<ItemInstance>(static_cast<int>(Tile::stone_Id), 1, 0);
            } else {
                m_iconItem = nullptr;
            }
        }
        if (!isFixedIcon && m_iconItem != nullptr) setupIconHolder(e_ICON_TYPE_IGGY);
        m_controlIconHolder.setVisible(isFixedIcon || m_iconItem != nullptr);

        return temp;
    }

    // 4J Stu - Unused
    wstring _SetImage(wstring &desc) { return desc; }

    wstring ParseDescription(int iPad, wstring &text) {
        text = replaceAll(text, L"{*CraftingTableIcon*}", L"");
        text = replaceAll(text, L"{*SticksIcon*}", L"");
        text = replaceAll(text, L"{*PlanksIcon*}", L"");
        text = replaceAll(text, L"{*WoodenShovelIcon*}", L"");
        text = replaceAll(text, L"{*WoodenHatchetIcon*}", L"");
        text = replaceAll(text, L"{*WoodenPickaxeIcon*}", L"");
        text = replaceAll(text, L"{*FurnaceIcon*}", L"");
        text = replaceAll(text, L"{*WoodenDoorIcon*}", L"");
        text = replaceAll(text, L"{*TorchIcon*}", L"");
        text = replaceAll(text, L"{*MinecartIcon*}", L"");
        text = replaceAll(text, L"{*BoatIcon*}", L"");
        text = replaceAll(text, L"{*FishingRodIcon*}", L"");
        text = replaceAll(text, L"{*FishIcon*}", L"");
        text = replaceAll(text, L"{*RailIcon*}", L"");
        text = replaceAll(text, L"{*PoweredRailIcon*}", L"");
        text = replaceAll(text, L"{*StructuresIcon*}", L"");
        text = replaceAll(text, L"{*ToolsIcon*}", L"");
        text = replaceAll(text, L"{*StoneIcon*}", L"");

        bool exitScreenshot = false;
        size_t pos = text.find(L"{*EXIT_PICTURE*}");
        if (pos != wstring::npos) exitScreenshot = true;
        text = replaceAll(text, L"{*EXIT_PICTURE*}", L"");
        m_controlExitScreenshot.setVisible(exitScreenshot);

        return mcle_format_html_string(iPad, text);
    }

    void UpdateInteractScenePosition(bool visible) {
        if (m_interactScene == nullptr) return;

        // 4J-PB - check this players screen section to see if we should allow
        // the animation. The one player here is fullscreen, which always does.
        bool bAllowAnim = true;

        if (bAllowAnim) {
            bool movingLeft = visible;

            if ((m_lastInteractSceneMoved != m_interactScene && movingLeft) ||
                (m_lastInteractSceneMoved == m_interactScene && m_lastSceneMovedLeft != movingLeft)) {
                if (movingLeft) {
                    mcle_container_scene(m_interactScene)->slideLeft();
                } else {
                    mcle_container_scene(m_interactScene)->slideRight();
                }

                m_lastInteractSceneMoved = m_interactScene;
                m_lastSceneMovedLeft = movingLeft;
            }
        }
    }

    void setupIconHolder(EIcons icon) {
        movieCall("", "SetupIconHolder", {argNum(static_cast<F64>(icon))});
        m_iconType = icon;
    }
};

TutorialPopup g_popup;

} // namespace

void mcle_tutorial_popup_set_tutorial(Tutorial *tutorial) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_popup.SetTutorial(tutorial);
}

void mcle_tutorial_popup_set_container_menu_visible(bool visible) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_popup.SetContainerMenuVisible(visible);
}

void mcle_tutorial_popup_set_description(MCLETutorialPopupInfo *info) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_popup.SetTutorialDescription(info);
}

void mcle_tutorial_popup_remove_interact_scene(UIScene *scene) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_popup.RemoveInteractSceneReference(scene);
}

void mcle_tutorial_popup_set_visible(bool visible) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_popup.SetVisible(visible);
}

bool mcle_tutorial_popup_is_visible() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    return g_popup.IsVisible();
}

void mcle_tutorial_popup_reset() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_popup.reset();
}

void mcle_tutorial_popup_movie_ready() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_popup.movieReady();
}

void mcle_tutorial_popup_tick() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_popup.tick();
}

std::vector<MCLEUIOp> mcle_tutorial_popup_take_ops() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    std::vector<MCLEUIOp> ops;
    ops.swap(g_ops);
    return ops;
}

void mcle_tutorial_popup_set_regions(const MCLECustomDrawRegion *regions, int count) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_regions.assign(regions, regions + (count > 0 ? count : 0));
}

bool mcle_tutorial_popup_has_icon() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    return g_popup.IsVisible() && g_popup.hasIcon() && !g_regions.empty();
}

void mcle_tutorial_popup_custom_draw() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_popup.IsVisible()) return;
    for (const MCLECustomDrawRegion &region : g_regions) g_popup.customDraw(region);
}
