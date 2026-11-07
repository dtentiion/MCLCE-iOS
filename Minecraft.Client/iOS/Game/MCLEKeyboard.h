#pragma once

// UIScene_Keyboard, Keyboard1080 over the scene that asked for it, which is
// what Windows64 puts up to type with a controller (MCLEKeyboard.cpp)

#include <string>

// UIKeyboardInitData. It waits for the keyboard's movie, so the strings
// are its own; what they're left as is what the scene takes for null.
struct MCLEKeyboardInitData {
    std::wstring title = L"Enter text";
    std::wstring defaultText;
    int maxChars = 25;
    int (*callback)(LPVOID, const bool) = nullptr;
    LPVOID lpParam = nullptr;
};

// ui.NavigateToScene(iPad, eUIScene_Keyboard, &kbData) from a container
// scene. If the keyboard's movie never comes up the callback is told it
// was cancelled.
void mcle_keyboard_navigate_to(int iPad, const MCLEKeyboardInitData &kbData);

// Win64_GetKeyboardText, the text the keyboard was last done with
const std::wstring &mcle_keyboard_text();
