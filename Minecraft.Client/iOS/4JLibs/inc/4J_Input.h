// iOS stub for the platform 4J input layer. Real input is dispatched
// through Minecraft.Client/iOS/Input/INP_iOS_Controller.mm using
// GameController.framework. This header exists so upstream gameplay
// code that includes it from stdafx.h does not fail to find the file.

#pragma once

#include "iOS_WinCompat.h"

#ifdef __cplusplus
// Real platform 4J_Input.h on Win64 / Xbox / Orbis exposes a global
// `C4JInput InputManager` plus methods upstream gameplay code reaches
// into (LocalPlayer queries idle time, MovePlayerPacket polls button
// state, etc.). Variadic methods absorb whatever the call site passes.
class C_4JInput {
public:
    template<class... A> int   GetIdleSeconds(A...)         { return 0; }
    template<class... A> bool  IsButtonPressed(A...)        { return false; }
    template<class... A> bool  IsButtonReleased(A...)       { return false; }
    template<class... A> bool  IsButtonHeld(A...)           { return false; }
    template<class... A> float GetAxisValue(A...)           { return 0.0f; }
    template<class... A> int   GetTriggerValue(A...)        { return 0; }
    template<class... A> void  SetVibration(A...)           {}
    template<class... A> void  ResetIdleTimer(A...)         {}
    template<class... A> bool  IsConnected(A...)            { return true; }
    template<class... A> int   GetValue(A...)               { return 0; }
    template<class... A> void* GetGameJoypadMaps(A...)      { return nullptr; }
};

extern C_4JInput InputManager;
#endif
