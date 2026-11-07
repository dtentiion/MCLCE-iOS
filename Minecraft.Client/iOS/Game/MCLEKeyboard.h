#pragma once

// UIScene_Keyboard, Keyboard1080 over the scene that asked for it, which is
// what Windows64 puts up to type with a controller (MCLEKeyboard.cpp)

#include <string>

// UIKeyboardInitData. It waits for the keyboard's movie, so the strings
// are its own; what they're left as is what the scene takes for null.
// The callback's LPVOID is a plain void * so the front end menus in the
// view controller can use it too.
struct MCLEKeyboardInitData {
    std::wstring title = L"Enter text";
    std::wstring defaultText;
    int maxChars = 25;
    int (*callback)(void *, const bool) = nullptr;
    void *lpParam = nullptr;
};

// ui.NavigateToScene(iPad, eUIScene_Keyboard, &kbData) from a container
// scene. If the keyboard's movie never comes up the callback is told it
// was cancelled.
void mcle_keyboard_navigate_to(int iPad, const MCLEKeyboardInitData &kbData);

// Win64_GetKeyboardText, the text the keyboard was last done with
const std::wstring &mcle_keyboard_text();

// The same from a front end menu, which runs in the view controller's own
// menu stack rather than the container scenes'. The view controller puts
// Keyboard1080 up over the menu and makes the scene once it's up (open,
// false when no front end menu asked for it). navigate_done after that
// tells the callback it was cancelled if the movie never came up.
void mcle_keyboard_front_end_navigate_to(int iPad, const MCLEKeyboardInitData &kbData);
bool mcle_keyboard_front_end_open();
void mcle_keyboard_front_end_navigate_done();
bool mcle_keyboard_front_end_is_open();

// One UI frame: the pad's presses (UIController::handleInput) then the
// scene's tick. `prevButtons` is the last frame's.
void mcle_keyboard_front_end_frame(unsigned int buttons, unsigned int prevButtons, double now);

// The movie's handlePress, and handleFocusChange / handleInitFocus
void mcle_keyboard_front_end_press(int controlId, int childId);
void mcle_keyboard_front_end_focus_change(int controlId, int childId, bool init);

// The scene's navigateBack, then its handleDestroy before the menu under
// it goes back up
bool mcle_keyboard_front_end_close_requested();
void mcle_keyboard_front_end_close();
