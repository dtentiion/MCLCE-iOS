// iOS stub for the platform 4J input layer. Real input is dispatched
// through Minecraft.Client/iOS/Input/INP_iOS_Controller.mm using
// GameController.framework. This header exists so upstream gameplay
// code that includes it from stdafx.h does not fail to find the file.

#pragma once

#include "iOS_WinCompat.h"

#ifdef __cplusplus
// F3-real-input bridge into the iOS GameController.framework reader.
// Defined in Input/INP_iOS_Bridge.cpp.
extern "C" int  mcle_ios_input_poll_lx(int pad);
extern "C" int  mcle_ios_input_poll_ly(int pad);
extern "C" int  mcle_ios_input_poll_rx(int pad);
extern "C" int  mcle_ios_input_poll_ry(int pad);
extern "C" int  mcle_ios_input_poll_lt(int pad);
extern "C" int  mcle_ios_input_poll_rt(int pad);
extern "C" int  mcle_ios_input_poll_buttons(int pad);
extern "C" int  mcle_ios_input_poll_connected(int pad);

// Real platform 4J_Input.h on Win64 / Xbox / Orbis exposes a global
// `C4JInput InputManager` plus methods upstream gameplay code reaches
// into. Non-template overloads match exact upstream signatures and
// route to the iOS GameController bridge; variadic catch-alls absorb
// the long-tail call sites until each is touched.
class C_4JInput {
public:
    template<class... A> int   GetIdleSeconds(A...)         { return 0; }
    template<class... A> bool  IsButtonPressed(A...)        { return false; }
    template<class... A> bool  ButtonPressed(A...)           { return false; }
    template<class... A> bool  IsButtonReleased(A...)       { return false; }
    template<class... A> bool  IsButtonHeld(A...)           { return false; }
    template<class... A> float GetAxisValue(A...)           { return 0.0f; }
    template<class... A> int   GetTriggerValue(A...)        { return 0; }
    template<class... A> void  SetVibration(A...)           {}
    template<class... A> void  ResetIdleTimer(A...)         {}
    template<class... A> int   GetValue(A...)               { return 0; }
    template<class... A> void* GetGameJoypadMaps(A...)      { return nullptr; }
    template<class... A> int   GetJoypadMapVal(A...)        { return 0; }

    // F3-real-input: sticks. Return values are 0..1000 in upstream's
    // 4J convention (signed for sticks, unsigned for triggers). The
    // INP_iOS_Bridge converts -1..1 floats into this range.
    inline float GetJoypadStick_LX(int pad)             { return mcle_ios_input_poll_lx(pad) / 1000.0f; }
    inline float GetJoypadStick_LX(int pad, bool /*deadzone*/) { return GetJoypadStick_LX(pad); }
    inline float GetJoypadStick_LY(int pad)             { return mcle_ios_input_poll_ly(pad) / 1000.0f; }
    inline float GetJoypadStick_LY(int pad, bool /*deadzone*/) { return GetJoypadStick_LY(pad); }
    inline float GetJoypadStick_RX(int pad)             { return mcle_ios_input_poll_rx(pad) / 1000.0f; }
    inline float GetJoypadStick_RX(int pad, bool /*deadzone*/) { return GetJoypadStick_RX(pad); }
    inline float GetJoypadStick_RY(int pad)             { return mcle_ios_input_poll_ry(pad) / 1000.0f; }
    inline float GetJoypadStick_RY(int pad, bool /*deadzone*/) { return GetJoypadStick_RY(pad); }
    inline float GetJoypadTrigger_L(int pad)            { return mcle_ios_input_poll_lt(pad) / 1000.0f; }
    inline float GetJoypadTrigger_R(int pad)            { return mcle_ios_input_poll_rt(pad) / 1000.0f; }
    template<class... A> float GetJoypadStick_LX(A...)  { return 0.0f; }
    template<class... A> float GetJoypadStick_LY(A...)  { return 0.0f; }
    template<class... A> float GetJoypadStick_RX(A...)  { return 0.0f; }
    template<class... A> float GetJoypadStick_RY(A...)  { return 0.0f; }
    template<class... A> float GetJoypadTrigger_L(A...) { return 0.0f; }
    template<class... A> float GetJoypadTrigger_R(A...) { return 0.0f; }

    inline bool IsPadConnected(int pad)                 { return mcle_ios_input_poll_connected(pad) != 0; }
    template<class... A> bool IsPadConnected(A...)      { return true; }
    inline bool IsConnected(int pad)                    { return mcle_ios_input_poll_connected(pad) != 0; }
    template<class... A> bool IsConnected(A...)         { return true; }
};

extern C_4JInput InputManager;
#endif
