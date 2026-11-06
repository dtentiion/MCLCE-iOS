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
// The control scheme tables, Windows64's DefineActions (Game/MCLEJoypadMaps.cpp)
extern "C" unsigned int mcle_joypad_map(int style, int action);
extern "C" int  mcle_joypad_map_val(int pad);

// The Windows64 4J_Input.h button bits, which the pad reader fills in
#ifndef _360_JOY_BUTTON_A
#define	_360_JOY_BUTTON_A						0x00000001
#define	_360_JOY_BUTTON_B						0x00000002
#define	_360_JOY_BUTTON_X						0x00000004
#define	_360_JOY_BUTTON_Y						0x00000008
#define	_360_JOY_BUTTON_START					0x00000010
#define	_360_JOY_BUTTON_BACK					0x00000020
#define	_360_JOY_BUTTON_RB						0x00000040
#define	_360_JOY_BUTTON_LB						0x00000080
#define	_360_JOY_BUTTON_RTHUMB					0x00000100
#define	_360_JOY_BUTTON_LTHUMB					0x00000200
#define	_360_JOY_BUTTON_DPAD_UP					0x00000400
#define	_360_JOY_BUTTON_DPAD_DOWN				0x00000800
#define	_360_JOY_BUTTON_DPAD_LEFT				0x00001000
#define	_360_JOY_BUTTON_DPAD_RIGHT				0x00002000
#define	_360_JOY_BUTTON_LSTICK_RIGHT			0x00004000
#define	_360_JOY_BUTTON_LSTICK_LEFT				0x00008000
#define	_360_JOY_BUTTON_RSTICK_DOWN				0x00010000
#define	_360_JOY_BUTTON_RSTICK_UP				0x00020000
#define	_360_JOY_BUTTON_RSTICK_RIGHT			0x00040000
#define	_360_JOY_BUTTON_RSTICK_LEFT				0x00080000
#define	_360_JOY_BUTTON_LSTICK_DOWN				0x00100000
#define	_360_JOY_BUTTON_LSTICK_UP				0x00200000
#define	_360_JOY_BUTTON_RT						0x00400000
#define	_360_JOY_BUTTON_LT						0x00800000
#endif

#define MAP_STYLE_0 0
#define MAP_STYLE_1 1
#define MAP_STYLE_2 2

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
    unsigned int GetGameJoypadMaps(int style, int action) { return mcle_joypad_map(style, action); }
    int          GetJoypadMapVal(int pad)                  { return mcle_joypad_map_val(pad); }

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
