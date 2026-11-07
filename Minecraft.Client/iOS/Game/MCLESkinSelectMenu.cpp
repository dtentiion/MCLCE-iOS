// Change Skin: UIScene_SkinSelectMenu and the UIControl_PlayerSkinPreview
// characters in its carousel. Their bodies are upstream's, copied out by
// scripts/gen-skin-select-menu.py and compiled below against stand-ins for
// the Iggy side: the controls queue their calls like the container menus'
// (MCLEContainerScene.h) and the view controller makes them on the movie.

#include "MCLESkinSelectMenu.h"
#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../../../upstream/Minecraft.World/net.minecraft.world.entity.player.h"
#include "../../../upstream/Minecraft.World/SoundTypes.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/Options.h"
#include "../../../upstream/Minecraft.Client/Textures.h"
#include "../../../upstream/Minecraft.Client/EntityRenderDispatcher.h"
#include "../../../upstream/Minecraft.Client/EntityRenderer.h"
#include "../../../upstream/Minecraft.Client/HumanoidModel.h"
#include "../../../upstream/Minecraft.Client/ModelPart.h"
#include "../../../upstream/Minecraft.Client/Lighting.h"
#include "../../../upstream/Minecraft.Client/ScreenSizeCalculator.h"

extern "C" void mcle_metal_clear_depth(void); // MetalContext.mm

// Player's cape paths, for CMinecraftApp::SetPlayerCape / GetPlayerCapeName
unsigned int mcle_cape_id_from_path(const std::wstring &cape) { return Player::getCapeIdFromPath(cape); }
std::wstring mcle_cape_path_from_id(unsigned int capeId) { return Player::getCapePathFromId(capeId); }

namespace mcle_skin {

// SkinSelectMenu1080 is a 30 frames a second movie
const double kMovieFrameTime = 1.0 / 30.0;

// The screen size UIControl_PlayerSkinPreview takes off Minecraft: the
// 1920x1080 the movie is laid out for, scaled the way ScreenSizeCalculator
// does with Options::guiScale on auto
void mcle_skin_preview_screen(float &screenWidth, float &rawWidth, float &screenHeight, float &rawHeight) {
    const int width = 1920;
    const int height = 1080;
    int scale = 1;
    while (width / (scale + 1) >= 320 && height / (scale + 1) >= 240) {
        scale++;
    }
    ScreenSizeCalculator ssc(nullptr, width, height, scale);
    screenWidth = static_cast<float>(width);
    rawWidth = static_cast<float>(ssc.rawWidth);
    screenHeight = static_cast<float>(height);
    rawHeight = static_cast<float>(ssc.rawHeight);
}

// The part of Iggy's custom draw region the preview reads. The region here
// is the one Ruffle reports, in pixels.
struct IggyCustomDrawCallbackRegion {
    float x0, y0, x1, y1;
};

// UIControl: its value path and visibility
class UIControl {
public:
    enum eUIControlType { ePlayerSkinPreview };

    virtual ~UIControl() {}
    virtual void tick() {}

    void setControlType(eUIControlType) {}

    // UIControl::setVisible
    void setVisible(bool visible) {
        if (visible != m_isVisible) {
            mcle_ui_set_visible(m_path, visible);
            m_isVisible = visible;
        }
    }

    std::string m_path;
    bool m_isVisible = true;
};

// UIControl_Label
class UIControl_Label : public UIControl {
public:
    void init(const std::wstring &label) {
        m_label.path = m_path;
        m_label.init(label);
    }

    void setLabel(const std::wstring &label, bool instant = false, bool force = false) {
        m_label.path = m_path;
        m_label.setLabel(label, instant, force);
    }

    void tick() override { m_label.tick(); }

private:
    MCLEUILabel m_label{""};
};

// UIControl_PlayerSkinPreview.h
class UIControl_PlayerSkinPreview : public UIControl {
private:
    static const int LOOK_LEFT_EXTENT = 45;
    static const int LOOK_RIGHT_EXTENT = -45;

    static const int CHANGING_SKIN_FRAMES = 15;

    enum ESkinPreviewAnimations {
        e_SkinPreviewAnimation_Walking,
        e_SkinPreviewAnimation_Sneaking,
        e_SkinPreviewAnimation_Attacking,

        e_SkinPreviewAnimation_Count,
    };

    BOOL m_bDirty;
    float m_fScale, m_fAlpha;

    wstring m_customTextureUrl;
    TEXTURE_NAME m_backupTexture;
    wstring m_capeTextureUrl;
    unsigned int m_uiAnimOverrideBitmask;

    float m_fScreenWidth, m_fScreenHeight;
    float m_fRawWidth, m_fRawHeight;

    int m_yRot, m_xRot;

    float m_bobTick;

    float m_walkAnimSpeedO;
    float m_walkAnimSpeed;
    float m_walkAnimPos;

    bool m_bAutoRotate, m_bRotatingLeft;
    BYTE m_rotateTick;
    float m_fTargetRotation, m_fOriginalRotation;
    int m_framesAnimatingRotation;
    bool m_bAnimatingToFacing;

    float m_swingTime;

    ESkinPreviewAnimations m_currentAnimation;
    vector<ModelPart *> *m_pvAdditionalModelParts;

public:
    enum ESkinPreviewFacing {
        e_SkinPreviewFacing_Forward,
        e_SkinPreviewFacing_Left,
        e_SkinPreviewFacing_Right,
    };

    UIControl_PlayerSkinPreview();

    void tick() override;

    void render(IggyCustomDrawCallbackRegion *region);

    void SetTexture(const wstring &url, TEXTURE_NAME backupTexture = TN_MOB_CHAR);
    void SetCapeTexture(const wstring &url) { m_capeTextureUrl = url; }
    void ResetRotation() {
        m_xRot = 0;
        m_yRot = 0;
    }
    void IncrementYRotation() {
        m_yRot = (m_yRot + 4);
        if (m_yRot >= 180) m_yRot = -180;
    }
    void DecrementYRotation() {
        m_yRot = (m_yRot - 4);
        if (m_yRot <= -180) m_yRot = 180;
    }
    void IncrementXRotation() {
        m_xRot = (m_xRot + 2);
        if (m_xRot > 22) m_xRot = 22;
    }
    void DecrementXRotation() {
        m_xRot = (m_xRot - 2);
        if (m_xRot < -22) m_xRot = -22;
    }
    void SetAutoRotate(bool autoRotate) { m_bAutoRotate = autoRotate; }
    void SetFacing(ESkinPreviewFacing facing, bool bAnimate = false);

    void CycleNextAnimation();
    void CyclePreviousAnimation();

    bool m_incXRot, m_decXRot;
    bool m_incYRot, m_decYRot;

private:
    void render(EntityRenderer *renderer, double x, double y, double z, float rot, float a);
    bool bindTexture(const wstring &urlTexture, int backupTexture);
    bool bindTexture(const wstring &urlTexture, const wstring &backupTexture);
};

// UIScene, the parts the skin select menu uses
class UIScene {
public:
    UIScene(int iPad, UILayer *) : m_iPad(iPad) {}
    virtual ~UIScene() {}

    virtual void tick();
    virtual void handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled) = 0;
    virtual void handleAnimationEnd() {}

    // UIScene::allowRepeat
    virtual bool allowRepeat(int key) {
        // 4J-PB - ignore repeats of action ABXY buttons
        // fix for PS3 213 - [MAIN MENU] Holding down buttons will continue to activate every prompt.
        switch (key) {
        case ACTION_MENU_OK:
        case ACTION_MENU_CANCEL:
        case ACTION_MENU_A:
        case ACTION_MENU_B:
        case ACTION_MENU_X:
        case ACTION_MENU_Y:
            return false;
        }
        return true;
    }

    void setTime(double now) { m_now = now; }
    bool closeRequested() const { return m_closeRequested; }

protected:
    int m_iPad;
    // The controls UIScene::tick ticks, in the order they're mapped
    std::vector<UIControl *> m_controls;

    void sendInputToMovie(int key, bool repeat, bool pressed, bool released);
    void navigateBack();

private:
    double m_now = 0;
    double m_nextTick = 0;
    bool m_closeRequested = false;
};

// The UIController calls the scene makes
struct MCLESkinSelectUI {
    void PlayUISFX(int sfx) { mcleContainerUI.PlayUISFX(sfx); }
    template <class... A> void SetTooltips(A... a) { mcleContainerUI.SetTooltips(a...); }
    template <class... A> void AnimateKeyPress(A... a) { mcleContainerUI.AnimateKeyPress(a...); }
    // The upsell for a DLC skin that isn't owned. Every pack counts as owned
    // here (DLCPack::hasPurchasedFile), so it's never asked.
    template <class... A> void RequestAlertMessage(A...) {}
};
MCLESkinSelectUI mcleSkinUI;

// UIScene_SkinSelectMenu.h
class UIScene_SkinSelectMenu : public UIScene {
private:
    static const WCHAR *wchDefaultNamesA[eDefaultSkins_Count];

    // 4J Stu - How many to show on each side of the main control
    static const BYTE sidePreviewControls = 4;

    enum ESkinSelectNavigation {
        eSkinNavigation_Pack,
        eSkinNavigation_Skin,

        eSkinNavigation_Count,
    };

    enum ECharacters {
        eCharacter_Current,
        eCharacter_Next1,
        eCharacter_Next2,
        eCharacter_Next3,
        eCharacter_Next4,
        eCharacter_Previous1,
        eCharacter_Previous2,
        eCharacter_Previous3,
        eCharacter_Previous4,

        eCharacter_COUNT,
    };

    UIControl_PlayerSkinPreview m_characters[eCharacter_COUNT];
    UIControl_Label m_labelSkinName, m_labelSkinOrigin;
    UIControl_Label m_labelSelected;
    UIControl m_controlSkinNamePlate, m_controlSelectedPanel, m_controlIggyCharacters, m_controlTimer;

    DLCPack *m_currentPack;
    DWORD m_packIndex, m_skinIndex;
    DWORD m_originalSkinId;
    wstring m_currentSkinPath, m_selectedSkinPath, m_selectedCapePath;
    vector<SKIN_BOX *> *m_vAdditionalSkinBoxes;

    bool m_bSlidingSkins, m_bAnimatingMove;
    ESkinSelectNavigation m_currentNavigation;

    bool m_bNoSkinsToShow;
    DWORD m_currentPackCount;
    bool m_bIgnoreInput;
    bool m_bSkinIndexChanged;
    wstring m_leftLabel, m_centreLabel, m_rightLabel;

    S32 m_iTouchXStart;
    bool m_bTouchScrolled;

public:
    UIScene_SkinSelectMenu(int iPad, void *initData, UILayer *parentLayer);

    void tick() override;

    virtual void updateTooltips();

    void handleAnimationEnd() override;

    // INPUT
    void handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled) override;

    void customDraw(const MCLECustomDrawRegion &region);

    void HandleDLCMountingComplete();

private:
    void initialiseMovie();

    void handleSkinIndexChanged();
    int getNextSkinIndex(DWORD sourceIndex);
    int getPreviousSkinIndex(DWORD sourceIndex);

    TEXTURE_NAME getTextureId(int skinIndex);

    void handlePackIndexChanged();
    void updatePackDisplay();
    int getNextPackIndex(DWORD sourceIndex);
    int getPreviousPackIndex(DWORD sourceIndex);

    void setCharacterSelected(bool selected);
    void setCharacterLocked(bool locked);

    void setLeftLabel(const wstring &label);
    void setCentreLabel(const wstring &label);
    void setRightLabel(const wstring &label);

    void showNotOnlineDialog(int iPad);

    static int UnlockSkinReturned(void *pParam, int iPad, C4JStorage::EMessageResult result);
    void AddFavoriteSkin(int iPad, int iSkinID);

    void InputActionOK(unsigned int iPad);
};

// UIScene::tick: the controls tick with the movie's frames
void UIScene::tick() {
    if (m_nextTick == 0 || m_now - m_nextTick > 0.5) m_nextTick = m_now;
    while (m_now >= m_nextTick) {
        for (UIControl *control : m_controls) {
            control->tick();
        }
        m_nextTick += kMovieFrameTime;
    }
}

// UIScene::convertGameActionToIggyKeycode, as Flash key codes
int convertGameActionToIggyKeycode(int action) {
    switch (action) {
    case ACTION_MENU_A:
    case ACTION_MENU_OK:
        return 13; // IGGY_KEYCODE_ENTER
    case ACTION_MENU_B:
    case ACTION_MENU_CANCEL:
        return 27; // IGGY_KEYCODE_ESCAPE
    case ACTION_MENU_X:
        return 112; // IGGY_KEYCODE_F1
    case ACTION_MENU_Y:
        return 113; // IGGY_KEYCODE_F2
    case ACTION_MENU_UP:
        return 38;
    case ACTION_MENU_DOWN:
        return 40;
    case ACTION_MENU_RIGHT:
        return 39;
    case ACTION_MENU_LEFT:
        return 37;
    case ACTION_MENU_PAGEUP:
        return 33;
    case ACTION_MENU_PAGEDOWN:
        return 34;
    case ACTION_MENU_RIGHT_SCROLL:
        return 114; // IGGY_KEYCODE_F3
    case ACTION_MENU_LEFT_SCROLL:
        return 115; // IGGY_KEYCODE_F4
    case ACTION_MENU_OTHER_STICK_PRESS:
        return 116; // IGGY_KEYCODE_F5
    case ACTION_MENU_OTHER_STICK_UP:
        return 122; // IGGY_KEYCODE_F11
    case ACTION_MENU_OTHER_STICK_DOWN:
        return 123; // IGGY_KEYCODE_F12
    }
    return -1;
}

// UIScene::sendInputToMovie
void UIScene::sendInputToMovie(int key, bool repeat, bool pressed, bool released) {
    int iggyKeyCode = convertGameActionToIggyKeycode(key);

    if (iggyKeyCode < 0) {
        app.DebugPrintf("UI WARNING: Ignoring input as game action does not translate to an Iggy keycode\n");
        return;
    }

    mcle_ui_key(iggyKeyCode, pressed);
}

// UIScene::navigateBack, the view controller goes back once the frame's done
void UIScene::navigateBack() {
    //CD - Added for audio
    mcleSkinUI.PlayUISFX(eSFX_Back);

    m_closeRequested = true;
}

// UIScene::initialiseMovie, UIScene_SkinSelectMenu's UI_MAP_ELEMENTs
void UIScene_SkinSelectMenu::initialiseMovie() {
    struct Mapping {
        UIControl *control;
        std::string path;
    };
    const std::string characters = "IggyCharacters.";
    std::vector<Mapping> map = {
        {&m_controlSkinNamePlate, "SkinNamePlate"},
        {&m_labelSkinName, "SkinNamePlate.SkinTitle1"},
        {&m_labelSkinOrigin, "SkinNamePlate.SkinTitle2"},
        {&m_controlSelectedPanel, "SelectedPanel"},
        {&m_labelSelected, "SelectedPanel.SelectedPanelLabel"},
        {&m_controlTimer, "Timer"},
        {&m_controlIggyCharacters, "IggyCharacters"},
        {&m_characters[eCharacter_Current], characters + "iggy_Character0"},
        {&m_characters[eCharacter_Next1], characters + "iggy_Character1"},
        {&m_characters[eCharacter_Next2], characters + "iggy_Character2"},
        {&m_characters[eCharacter_Next3], characters + "iggy_Character3"},
        {&m_characters[eCharacter_Next4], characters + "iggy_Character4"},
        {&m_characters[eCharacter_Previous1], characters + "iggy_Character5"},
        {&m_characters[eCharacter_Previous2], characters + "iggy_Character6"},
        {&m_characters[eCharacter_Previous3], characters + "iggy_Character7"},
        {&m_characters[eCharacter_Previous4], characters + "iggy_Character8"},
    };
    for (const Mapping &m : map) {
        m.control->m_path = m.path;
        m_controls.push_back(m.control);
    }
}

void UIScene_SkinSelectMenu::setCharacterSelected(bool selected) {
    mcle_ui_call("", "SetPlayerCharacterSelected", {mcle_ui_bool(selected)});
}

void UIScene_SkinSelectMenu::setCharacterLocked(bool locked) {
    mcle_ui_call("", "SetCharacterLocked", {mcle_ui_bool(locked)});
}

void UIScene_SkinSelectMenu::setLeftLabel(const wstring &label) {
    if (label.compare(m_leftLabel) != 0) {
        m_leftLabel = label;

        mcle_ui_call("", "SetLeftLabel", {mcle_ui_str(label)});
    }
}

void UIScene_SkinSelectMenu::setCentreLabel(const wstring &label) {
    if (label.compare(m_centreLabel) != 0) {
        m_centreLabel = label;

        mcle_ui_call("", "SetCenterLabel", {mcle_ui_str(label)});
    }
}

void UIScene_SkinSelectMenu::setRightLabel(const wstring &label) {
    if (label.compare(m_rightLabel) != 0) {
        m_rightLabel = label;

        mcle_ui_call("", "SetRightLabel", {mcle_ui_str(label)});
    }
}

// UIScene_SkinSelectMenu::customDraw. Iggy's matrix takes the object's own
// units to the screen; Ruffle's region is in pixels already, so the origin
// moves to it and pixels are the units. The movie has no masks, so there's
// no stencil to set.
void UIScene_SkinSelectMenu::customDraw(const MCLECustomDrawRegion &region) {
    int characterId = -1;
    sscanf(region.name, "Character%d", &characterId);
    if (characterId == -1) {
        app.DebugPrintf("Invalid character to render found\n");
    } else if (characterId >= 0 && characterId < eCharacter_COUNT) {
        // UIController::setupCustomDrawGameState and setupCustomDrawMatrices
        mcle_custom_draw_setup_state();
        glLoadIdentity();
        glTranslatef(0, 0, -2000);
        glTranslatef(region.x0, region.y0, 0);

        IggyCustomDrawCallbackRegion local = {0, 0, region.x1 - region.x0, region.y1 - region.y0};
        try {
            m_characters[characterId].render(&local);
        } catch (...) {
        }

        // UIController::endCustomDrawGameState, the depth is cleared for the
        // next region
        mcle_metal_clear_depth();
        glDepthMask(false);
        glDisable(GL_ALPHA_TEST);
    }
}

} // namespace mcle_skin

namespace mcle_skin {
#define ui mcleSkinUI
#include "../../../upstream/Minecraft.Client/iOS_SkinSelectMenu.inc"
#undef ui
} // namespace mcle_skin

namespace {

mcle_skin::UIScene_SkinSelectMenu *g_scene = nullptr;

// UIController::handleInput's key repeat
const double kRepeatDelay = 0.3; // UI_REPEAT_KEY_DELAY_MS
const double kRepeatRate = 0.1;  // UI_REPEAT_KEY_REPEAT_RATE_MS
double g_repeatTimer[ACTION_MAX_MENU + 1];

} // namespace

void mcle_skin_select_open(double now) {
    if (g_scene) mcle_skin_select_close();
    mcle_container_prepare();
    for (double &t : g_repeatTimer) t = 0;
    g_scene = new mcle_skin::UIScene_SkinSelectMenu(0, nullptr, nullptr);
    g_scene->setTime(now);
    g_scene->updateTooltips();
}

bool mcle_skin_select_is_open() {
    return g_scene != nullptr;
}

// UIController::handleInput then the scene's tick
void mcle_skin_select_frame(unsigned int buttons, unsigned int prevButtons, double now) {
    if (!g_scene || g_scene->closeRequested()) return;
    g_scene->setTime(now);

    for (int key = 0; key <= ACTION_MAX_MENU && !g_scene->closeRequested(); ++key) {
        unsigned int mapped = InputManager.GetGameJoypadMaps(InputManager.GetJoypadMapVal(0), key);
        bool down = (buttons & mapped) != 0;
        bool pressed = down && (prevButtons & mapped) == 0;
        bool released = !down && (prevButtons & mapped) != 0;
        bool repeat = false;
        if (pressed) {
            g_repeatTimer[key] = now + kRepeatDelay;
        } else if (released) {
            g_repeatTimer[key] = 0;
        } else if (down && g_repeatTimer[key] > 0 && now > g_repeatTimer[key]) {
            repeat = true;
            pressed = true;
            g_repeatTimer[key] = now + kRepeatRate;
        }
        if (!(repeat || pressed || released)) continue;
        // UILayer::handleInput
        if (repeat && !g_scene->allowRepeat(key)) continue;
        bool handled = false;
        g_scene->handleInput(0, key, repeat, pressed, released, handled);
    }

    if (!g_scene->closeRequested()) g_scene->tick();
}

bool mcle_skin_select_close_requested() {
    return g_scene != nullptr && g_scene->closeRequested();
}

void mcle_skin_select_close() {
    delete g_scene;
    g_scene = nullptr;
    mcle_container_prepare();
}

void mcle_skin_select_animation_end() {
    if (g_scene) g_scene->handleAnimationEnd();
}

// UIController::HandleDLCMountingComplete, to the scenes that are up
void mcle_ui_handle_dlc_mounting_complete() {
    if (g_scene) g_scene->HandleDLCMountingComplete();
}

// The characters draw with what Minecraft::init made at launch
// (mcle_minecraft_init)
void mcle_skin_select_custom_draw(const MCLECustomDrawRegion *regions, int count) {
    if (!g_scene || count <= 0) return;
    Minecraft *pMinecraft = Minecraft::GetInstance();
    if (!pMinecraft || !pMinecraft->options || !pMinecraft->textures || !EntityRenderDispatcher::instance) return;
    if (!mcle_custom_draw_begin()) return;
    for (int i = 0; i < count; ++i) g_scene->customDraw(regions[i]);
    mcle_custom_draw_end();
}
