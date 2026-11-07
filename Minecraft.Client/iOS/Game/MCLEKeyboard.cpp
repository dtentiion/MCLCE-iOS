// UIScene_Keyboard as Windows64 has it with a controller (not pcMode): the
// movie's own keys type into KeyboardTextInput, which is read back when
// Done goes. The physical keyboard part of its tick has no keys to read
// here. KeyboardScene stands in for the UIScene like the container menus'
// scenes do (MCLEContainerScene.h).

#include "MCLEKeyboard.h"
#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"

#include <string>

namespace {

// g_Win64KeyboardResult. wchar_t is 4 bytes here, so it's kept as a
// wstring rather than read back into a uint16_t buffer.
std::wstring g_keyboardResult;

#define KEYBOARD_DONE_TIMER_ID 0
#define KEYBOARD_DONE_TIMER_TIME 100

class KeyboardScene : public MCLEMenuScene {
public:
    KeyboardScene(int iPad, const MCLEKeyboardInitData &kbData)
        : m_EnterTextLabel("EnterTextLabel"), m_KeyboardTextInput("KeyboardTextInput"),
          m_ButtonSpace("Button_space"), m_ButtonCursorLeft("Button_CursorLeft"),
          m_ButtonCursorRight("Button_CursorRight"), m_ButtonCaps("Button_Caps"), m_ButtonDone("Button_Done"),
          m_ButtonSymbols("Button_symbols"), m_ButtonBackspace("Button_bspace") {
        m_iPad = iPad;

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
