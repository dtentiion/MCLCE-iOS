// UIScene_Keyboard as Windows64 has it with a controller (not pcMode): the
// movie's own keys type into KeyboardTextInput, which is read back when
// Done goes. The physical keyboard part of its tick has no keys to read
// here. KeyboardScene stands in for the UIScene like the container menus'
// scenes do (MCLEContainerScene.h). Over a front end menu it isn't in their
// stack, so it's held and driven here like UIScene_SkinSelectMenu is.

#include "MCLEKeyboard.h"
#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"

#include <string>

namespace {

// g_Win64KeyboardResult. wchar_t is 4 bytes here, so it's kept as a
// wstring rather than read back into a uint16_t buffer.
std::wstring g_keyboardResult;

// The navigateBack of the keyboard over a front end menu
bool g_frontEndCloseRequested = false;

#define KEYBOARD_DONE_TIMER_ID 0
#define KEYBOARD_DONE_TIMER_TIME 100

class KeyboardScene : public MCLEMenuScene {
public:
    KeyboardScene(int iPad, const MCLEKeyboardInitData &kbData, bool frontEnd = false)
        : m_EnterTextLabel("EnterTextLabel"), m_KeyboardTextInput("KeyboardTextInput"),
          m_ButtonSpace("Button_space"), m_ButtonCursorLeft("Button_CursorLeft"),
          m_ButtonCursorRight("Button_CursorRight"), m_ButtonCaps("Button_Caps"), m_ButtonDone("Button_Done"),
          m_ButtonSymbols("Button_symbols"), m_ButtonBackspace("Button_bspace") {
        m_iPad = iPad;
        m_frontEnd = frontEnd;

        m_win64Callback = kbData.callback;
        m_win64CallbackParam = kbData.lpParam;
        m_win64MaxChars = kbData.maxChars;

        m_EnterTextLabel.init(kbData.title);
        m_KeyboardTextInput.init(kbData.defaultText, -1);
        m_KeyboardTextInput.SetCharLimit(m_win64MaxChars);

        // Clear any leftover typed characters from a previous keyboard session
        g_keyboardResult.clear();

        m_ButtonSpace.init(L"Space", -1);
        m_ButtonCursorLeft.init(L"Cur L", -1);
        m_ButtonCursorRight.init(L"Cur R", -1);
        m_ButtonCaps.init(L"Caps", -1);
        m_ButtonDone.init(L"Done", 0); // only the done button needs an id, the others will never call back!
        m_ButtonSymbols.init(L"Symbols", -1);
        m_ButtonBackspace.init(L"Backspace", -1);

        // Initialise function keyboard Buttons and set alternative symbol button string
        mcle_ui_call("", "InitFunctionButtons", {mcle_ui_str(L"Abc")});

        m_bKeyboardDonePressed = false;

        gainFocus();
    }

    // parentLayer->addComponent(iPad, eUIComponent_MenuBackground)
    bool hasMenuBackground() override { return true; }

    const char *movieName() override { return "Keyboard1080.swf"; }

    // 4J - TomK - we want to allow X and Y repeats!
    bool allowRepeat(int key) override {
        switch (key) {
        case ACTION_MENU_OK:
        case ACTION_MENU_CANCEL:
        case ACTION_MENU_A:
        case ACTION_MENU_B:
        case ACTION_MENU_PAUSEMENU:
            return false;
        }
        return true;
    }

    void handleInput(int key, bool repeat, bool pressed, bool released) override {
        if (repeat || pressed) {
            switch (key) {
            case ACTION_MENU_CANCEL: {
                // Cache before navigateBack() destroys this scene
                int (*cb)(LPVOID, const bool) = m_win64Callback;
                LPVOID cbParam = m_win64CallbackParam;
                navigateBack();
                if (cb) cb(cbParam, false);
                break;
            }
            case ACTION_MENU_X:            // X
            case ACTION_MENU_PAGEUP:       // LT
            case ACTION_MENU_Y:            // Y
            case ACTION_MENU_STICK_PRESS:  // LS
            case ACTION_MENU_LEFT_SCROLL:  // LB
            case ACTION_MENU_RIGHT_SCROLL: // RB
                if (key == ACTION_MENU_X) mcle_ui_call("", "BackspaceButtonPressed", {});
                else if (key == ACTION_MENU_PAGEUP) mcle_ui_call("", "SymbolButtonPressed", {});
                else if (key == ACTION_MENU_Y) mcle_ui_call("", "SpaceButtonPressed", {});
                else if (key == ACTION_MENU_STICK_PRESS) mcle_ui_call("", "CapsButtonPressed", {});
                else if (key == ACTION_MENU_LEFT_SCROLL) mcle_ui_call("", "CursorLeftButtonPressed", {});
                else if (key == ACTION_MENU_RIGHT_SCROLL) mcle_ui_call("", "CursorRightButtonPressed", {});
                break;
            case ACTION_MENU_PAUSEMENU: // Start
                if (!m_bKeyboardDonePressed) {
                    mcle_ui_call("", "DoneButtonPressed", {});

                    // kick off done timer
                    addTimer(KEYBOARD_DONE_TIMER_ID, KEYBOARD_DONE_TIMER_TIME);
                    m_bKeyboardDonePressed = true;
                }
                break;
            }
        }

        switch (key) {
        case ACTION_MENU_OK:
        case ACTION_MENU_LEFT:
        case ACTION_MENU_RIGHT:
        case ACTION_MENU_UP:
        case ACTION_MENU_DOWN:
            sendInputToMovie(key, repeat, pressed, released);
            break;
        }
    }

    void handlePress(int controlId, int childId) override {
        if (controlId == 0) {
            // Done has been pressed. At this point we can query for the input string and pass it on to wherever it is needed.
            // we can not query for m_KeyboardTextInput.getLabel() here because we're in an iggy callback so we need to wait a frame.
            if (!m_bKeyboardDonePressed) {
                // kick off done timer
                addTimer(KEYBOARD_DONE_TIMER_ID, KEYBOARD_DONE_TIMER_TIME);
                m_bKeyboardDonePressed = true;
            }
        }
    }

    // UIScene::tick: the timer, then the labels that changed
    void tick(double now) override {
        if (m_timerMs > 0) {
            if (m_timerStart < 0) m_timerStart = now;
            if ((now - m_timerStart) * 1000.0 >= m_timerMs) handleTimerComplete(KEYBOARD_DONE_TIMER_ID);
        }
        m_EnterTextLabel.tick();
        m_KeyboardTextInput.tick();
    }

    void handleDestroy() override {}

private:
    bool m_bKeyboardDonePressed = false;
    bool m_frontEnd = false;

    int (*m_win64Callback)(LPVOID, const bool) = nullptr;
    LPVOID m_win64CallbackParam = nullptr;
    int m_win64MaxChars = 25;

    MCLEUILabel m_EnterTextLabel;
    MCLEUIControl m_KeyboardTextInput;
    MCLEUIControl m_ButtonSpace, m_ButtonCursorLeft, m_ButtonCursorRight, m_ButtonCaps, m_ButtonDone,
        m_ButtonSymbols, m_ButtonBackspace;

    // UIScene's timer for the one id the scene uses, timed from the tick
    // after it's added
    int m_timerMs = 0;
    double m_timerStart = -1;

    void addTimer(int, int ms) {
        m_timerMs = ms;
        m_timerStart = -1;
    }
    void killTimer(int) { m_timerMs = 0; }

    // UIScene::navigateBack. Over a front end menu it's the view
    // controller's menu stack that goes back.
    void navigateBack() {
        if (!m_frontEnd) {
            MCLEMenuScene::navigateBack();
            return;
        }
        //CD - Added for audio
        mcleContainerUI.PlayUISFX(eSFX_Back);
        g_frontEndCloseRequested = true;
    }

    void updateTooltips() override {
        mcleContainerUI.SetTooltips(m_iPad, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK, -1, -1);
    }

    void handleTimerComplete(int id) {
        if (id == KEYBOARD_DONE_TIMER_ID) {
            // remove timer
            killTimer(KEYBOARD_DONE_TIMER_ID);

            // we're done here!
            KeyboardDonePressed();
        }
    }

    void KeyboardDonePressed() {
        // Use getLabel() here, this is a timer callback (not an Iggy callback) so it's safe.
        // getLabel() reflects the on-screen button input (set directly by Flash ActionScript).
        std::wstring finalText = m_KeyboardTextInput.getLabel();
        app.DebugPrintf("UI Keyboard - DONE - [%ls]\n", finalText.c_str());

        // Store the typed text so callbacks can retrieve it via Win64_GetKeyboardText()
        g_keyboardResult = finalText.substr(0, 255);

        // Cache callback and param before navigateBack() which destroys this scene
        int (*cb)(LPVOID, const bool) = m_win64Callback;
        LPVOID cbParam = m_win64CallbackParam;

        // Navigate back so the scene stack is restored before the callback runs
        navigateBack();

        // Fire callback: bRes=true means confirmed
        if (cb) cb(cbParam, true);
    }
};

// A front end menu's NavigateToScene waiting for the keyboard's movie, then
// the keyboard over it
bool g_frontEndPending = false;
int g_frontEndPad = 0;
MCLEKeyboardInitData g_frontEndInitData;
KeyboardScene *g_frontEndScene = nullptr;

// UIController::handleInput's key repeat
const double kRepeatDelay = 0.3; // UI_REPEAT_KEY_DELAY_MS
const double kRepeatRate = 0.1;  // UI_REPEAT_KEY_REPEAT_RATE_MS
double g_repeatTimer[ACTION_MAX_MENU + 1];

} // namespace

void mcle_keyboard_navigate_to(int iPad, const MCLEKeyboardInitData &kbData) {
    mcle_container_navigate_to(
        "Keyboard1080.swf", [iPad, kbData]() -> MCLEContainerScene * { return new KeyboardScene(iPad, kbData); },
        [kbData]() {
            if (kbData.callback) kbData.callback(kbData.lpParam, false);
        });
}

const std::wstring &mcle_keyboard_text() {
    return g_keyboardResult;
}

void mcle_keyboard_front_end_navigate_to(int iPad, const MCLEKeyboardInitData &kbData) {
    g_frontEndPending = true;
    g_frontEndPad = iPad;
    g_frontEndInitData = kbData;
}

bool mcle_keyboard_front_end_open() {
    if (!g_frontEndPending) return false;
    g_frontEndPending = false;
    mcle_keyboard_front_end_close();
    mcle_container_prepare();
    for (double &t : g_repeatTimer) t = 0;
    g_frontEndScene = new KeyboardScene(g_frontEndPad, g_frontEndInitData, true);
    return true;
}

void mcle_keyboard_front_end_navigate_done() {
    if (!g_frontEndPending) return;
    // The movie never came up, so the menu that asked is still the one up
    g_frontEndPending = false;
    if (g_frontEndInitData.callback) g_frontEndInitData.callback(g_frontEndInitData.lpParam, false);
}

bool mcle_keyboard_front_end_is_open() {
    return g_frontEndScene != nullptr;
}

void mcle_keyboard_front_end_frame(unsigned int buttons, unsigned int prevButtons, double now) {
    if (!g_frontEndScene || g_frontEndCloseRequested) return;

    // UIController::handleInput: handleKeyPress for every menu action, then
    // UILayer::handleInput
    for (int key = 0; key <= ACTION_MAX_MENU && !g_frontEndCloseRequested; ++key) {
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
        if (repeat && !g_frontEndScene->allowRepeat(key)) continue;
        g_frontEndScene->handleInput(key, repeat, pressed, released);
    }

    if (!g_frontEndCloseRequested) g_frontEndScene->tick(now);
}

void mcle_keyboard_front_end_press(int controlId, int childId) {
    if (g_frontEndScene && !g_frontEndCloseRequested) g_frontEndScene->handlePress(controlId, childId);
}

void mcle_keyboard_front_end_focus_change(int controlId, int childId, bool init) {
    if (g_frontEndScene && !g_frontEndCloseRequested) g_frontEndScene->handleFocusChange(controlId, childId, init);
}

bool mcle_keyboard_front_end_close_requested() {
    return g_frontEndScene != nullptr && g_frontEndCloseRequested;
}

void mcle_keyboard_front_end_close() {
    if (!g_frontEndScene) return;
    g_frontEndScene->handleDestroy();
    delete g_frontEndScene;
    g_frontEndScene = nullptr;
    g_frontEndCloseRequested = false;
    // Nothing it left goes to the menu under it
    mcle_container_prepare();
}
