// Pure-C++ side of the iOS input bridge. Kept separate from the .mm file so
// it can be consumed by plain C++ code without pulling Objective-C into the
// translation unit.
//
// The heavy lifting lives in INP_iOS_Controller.mm. This file only exists so
// the static library has a non-Obj-C compile unit, which helps catch header
// hygiene problems early (no accidental Obj-C leakage into the C++ world).

#include "INP_iOS_Bridge.h"

// 4J_Input.h leans on Win32 types. Pull compat shims first.
#include "iOS_WinCompat.h"
#include "4J_Input.h"  // for _360_JOY_BUTTON_* macros

// Compile-time checks that the bridge header is valid C++.
static_assert(sizeof(mcle_ios_pad_state) > 0, "mcle_ios_pad_state is empty");

// CInput::Initialise's dead zone and range, in XInput's units
static const int kDeadzone = 10000;
static const int kMovementRangeMax = 32767;
static const int kEffectiveRange = kMovementRangeMax - kDeadzone;
static const int kHalfRange = kEffectiveRange / 2;

static int xinput_axis(float v) {
    int r = (int)(v * 32767.0f);
    if (r > 32767) return 32767;
    if (r < -32768) return -32768;
    return r;
}

static float past_dead_zone(int raw) {
    int v = 0;
    if (raw > kDeadzone) v = raw - kDeadzone;
    else if (raw < -kDeadzone) v = raw + kDeadzone;
    return v / (float)kEffectiveRange;
}

// CInput::SetJoypadValues. The stick and trigger buttons come from the pad
// as it is, whichever way round the sticks are set, and the game's sticks
// are what's left past the dead zone.
extern "C" void mcle_ios_pad_state_finish(mcle_ios_pad_state* s, int southpaw) {
    const int lx = xinput_axis(s->lx), ly = xinput_axis(s->ly);
    const int rx = xinput_axis(s->rx), ry = xinput_axis(s->ry);

    if ((int)(s->lt * 255.0f) > 0x7F) s->buttons |= _360_JOY_BUTTON_LT;
    if ((int)(s->rt * 255.0f) > 0x7F) s->buttons |= _360_JOY_BUTTON_RT;

    const int kStick = kHalfRange + kDeadzone;
    if (lx > kStick) s->buttons |= _360_JOY_BUTTON_LSTICK_RIGHT;
    if (lx < -kStick) s->buttons |= _360_JOY_BUTTON_LSTICK_LEFT;
    if (ry < -kStick) s->buttons |= _360_JOY_BUTTON_RSTICK_DOWN;
    if (ry > kStick) s->buttons |= _360_JOY_BUTTON_RSTICK_UP;
    if (rx > kStick) s->buttons |= _360_JOY_BUTTON_RSTICK_RIGHT;
    if (rx < -kStick) s->buttons |= _360_JOY_BUTTON_RSTICK_LEFT;
    if (ly < -kStick) s->buttons |= _360_JOY_BUTTON_LSTICK_DOWN;
    if (ly > kStick) s->buttons |= _360_JOY_BUTTON_LSTICK_UP;

    s->lx = past_dead_zone(lx);
    s->ly = past_dead_zone(ly);
    s->rx = past_dead_zone(rx);
    s->ry = past_dead_zone(ry);

    // ActionGameSettings(eGameSetting_ControlSouthPaw): SetJoypadStickAxisMap
    // and SetJoypadStickTriggerMap only change which values the game is
    // handed, the buttons above stay as they are
    if (southpaw) {
        float t;
        t = s->lx; s->lx = s->rx; s->rx = t;
        t = s->ly; s->ly = s->ry; s->ry = t;
        t = s->lt; s->lt = s->rt; s->rt = t;
    }
}

// Shims into upstream C_4JInput. Each returns -1000..1000
// for stick axes and 0..1000 for triggers; 4J_Input.h scales by /1000
// to recover the -1..1 / 0..1 range upstream gameplay code expects.
// Stick Y is positive up, same as GameController.framework: Input.cpp
// walks forward on a positive LY and the container pointer moves up on it.

static int clamp_axis(float v) {
    int r = (int)(v * 1000.0f);
    if (r > 1000) return 1000;
    if (r < -1000) return -1000;
    return r;
}

extern "C" int mcle_ios_input_poll_lx(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    return clamp_axis(s.lx);
}
extern "C" int mcle_ios_input_poll_ly(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    return clamp_axis(s.ly);
}
extern "C" int mcle_ios_input_poll_rx(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    return clamp_axis(s.rx);
}
extern "C" int mcle_ios_input_poll_ry(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    return clamp_axis(s.ry);
}
extern "C" int mcle_ios_input_poll_lt(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    int r = (int)(s.lt * 1000.0f);
    if (r > 1000) r = 1000;
    if (r < 0) r = 0;
    return r;
}
extern "C" int mcle_ios_input_poll_rt(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    int r = (int)(s.rt * 1000.0f);
    if (r > 1000) r = 1000;
    if (r < 0) r = 0;
    return r;
}
extern "C" int mcle_ios_input_poll_buttons(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    return (int)s.buttons;
}
extern "C" int mcle_ios_input_poll_connected(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    return s.connected;
}
