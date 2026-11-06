#pragma once

// What the container scenes have in common, for the Game files that make
// them: the UIScene they stand in for, the UIController calls their logic
// makes, and the controls whose Iggy calls get queued for the view
// controller (see MCLEContainerMenu.h).

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "MCLEContainerMenu.h"

class ServerPlayer;
class UIScene;
class IUIScene_CraftingMenu;
class MerchantMenu;

std::mutex &mcle_sim_tick_mutex();                 // MCLEGameLoop.cpp
std::shared_ptr<ServerPlayer> mcle_local_player(); // MCLEGameLoop.cpp
// Runs fn with a stand-in connection on the player, for the server code
// that sends its client packets (MCLEGameLoop.cpp)
void mcle_run_with_stand_in_connection(const std::shared_ptr<ServerPlayer> &player, void (*fn)(void *), void *arg);

// The UIScene parts the frame loop drives. Everything is called on the
// main thread holding the sim tick lock, except customDraw.
class MCLEContainerScene {
public:
    virtual ~MCLEContainerScene() {}

    // UIScene::allowRepeat: no repeats on the face buttons
    virtual bool allowRepeat(int key);
    virtual void handleInput(int key, bool repeat, bool pressed, bool released) = 0;
    virtual void tick(double now) = 0;
    virtual void handleDestroy() = 0;

    // The point IggyMakeEventMouseMove moves to, for scenes with a pointer
    virtual bool pointer(float *x, float *y) { return false; }

    // Copies what customDraw reads off the player
    virtual void snapshot() {}
    virtual void customDraw(const MCLECustomDrawRegion &region) = 0;

    // The callback a scene gave InputManager.RequestKeyboard
    virtual void keyboardComplete(bool ok, const std::wstring &text) {}

    // UIScene::slideLeft / slideRight, the tutorial popup moving the menu
    // out of its way
    virtual void slideLeft();
    virtual void slideRight();

    // What XuiCraftingTask casts the tutorial's scene to, the crafting menus'
    virtual IUIScene_CraftingMenu *craftingMenu() { return nullptr; }
};

// The UIScene a scene is to the tutorial, the one it gives
// Tutorial::changeTutorialState and the popup hands back
inline UIScene *mcle_ui_scene(MCLEContainerScene *scene) {
    return reinterpret_cast<UIScene *>(scene);
}
inline MCLEContainerScene *mcle_container_scene(UIScene *scene) {
    return reinterpret_cast<MCLEContainerScene *>(scene);
}
// The scene that's up, null when none is
MCLEContainerScene *mcle_container_current_scene();

// UIController::SetMenuDisplayed. NavigateToScene sets it before making the
// scene, which the scene's tutorial state goes by (MCLEGameLoop.cpp).
void mcle_game_set_menu_displayed(bool displayed);
// Clears what the last scene left, before a new one is made
void mcle_container_prepare();
// Hands a constructed scene to the frame loop
void mcle_container_begin(MCLEContainerScene *scene);
// The container id of the last ContainerOpenPacket taken
int mcle_container_open_id();
// UIScene_TradingMenu over the server's MerchantMenu, for the
// TRADER_NPC ContainerOpenPacket and its title (MCLETradingMenu.cpp)
MCLEContainerScene *mcle_trading_scene(MerchantMenu *menu, const std::shared_ptr<ServerPlayer> &player,
                                       const std::wstring &title);
// A ContainerOpenPacket for one of the server containers
// MCLEContainerMenus.cpp has a scene for, kept for it. False for the rest.
bool mcle_container_take_server_packet(int type, int containerId, const std::wstring &title, bool customName,
                                       int size, int entityId);

// One IggyPlayerCallMethodRS on the scene's movie, "" for the root
void mcle_ui_call(const std::string &path, const char *method, std::vector<MCLEUIArg> args);
MCLEUIArg mcle_ui_num(double v);
MCLEUIArg mcle_ui_bool(bool v);
MCLEUIArg mcle_ui_str(const std::wstring &v);
// UIScene::sendInputToMovie, a key down or up by its Iggy key code
void mcle_ui_key(int iggyKeyCode, bool down);
// UIControl::setVisible on a value path
void mcle_ui_set_visible(const std::string &path, bool visible);
// InputManager.RequestKeyboard: the system keyboard, answered through the
// scene's keyboardComplete
void mcle_ui_request_keyboard(const std::wstring &title, const std::wstring &text, int maxChars);

// The UIController calls the scene logic makes
struct MCLEContainerUI {
    void PlayUISFX(int sfx);
    void ShowTooltip(int iPad, int button, bool show);
    void SetTooltipText(int iPad, int button, int ids);
    void SetTooltips(int iPad, int iA, int iB = -1, int iX = -1, int iY = -1, int iLT = -1, int iRT = -1,
                     int iLB = -1, int iRB = -1, int iLS = -1, int iRS = -1, int iBack = -1,
                     bool forceUpdate = false);
    // UIController::IsTutorialVisible, the popup's
    bool IsTutorialVisible(int iPad);
    // The strip animates from the keys Ruffle gets from the pad
    template <class... A> void AnimateKeyPress(A...) {}
    // UIComponent_Tooltips plays nothing either way
    template <class... A> void OverrideSFX(A...) {}
    void NavigateBack(int);
    void CloseUIScenes(int);
    template <class... A> bool NavigateToScene(A...) { return false; }
};
extern MCLEContainerUI mcleContainerUI;

// UIControl_Label: Init now, SetLabel on the next tick when it changed
struct MCLEUILabel {
    std::string path;
    std::wstring label;
    bool changed = false;

    explicit MCLEUILabel(const char *p) : path(p) {}
    void init(const std::wstring &text);
    void setLabel(const std::wstring &text, bool instant = false, bool force = false);
    void tick();
};

// UIControl_Progress: Init with its label, setProgress when the fraction
// changes
struct MCLEUIProgress {
    std::string path;
    int min = 0, max = 100;
    float lastPercent = 0.0f;

    explicit MCLEUIProgress(const char *p) : path(p) {}
    void init(const std::wstring &label, int id, int min, int max, int current);
    void setProgress(int current);
};

// UIControl_SlotList's calls on its value path
struct MCLEUISlotList {
    std::string path;
    MCLEControlRect rect{};
    int lastHighlighted = 0;

    explicit MCLEUISlotList(const char *p) : path(p) {}
    void addSlot(int id);
    void addSlots(int start, int count);
    void setHighlightSlot(int index);
    void setSlotHighlighted(int index, bool highlight);
    void showSlotRedBox(int index, bool show);
    void setFocus(bool focus);
};
