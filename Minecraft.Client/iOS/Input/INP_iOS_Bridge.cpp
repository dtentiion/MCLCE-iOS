// Pure-C++ side of the iOS input bridge. Kept separate from the .mm file so
// it can be consumed by plain C++ code without pulling Objective-C into the
// translation unit.
//
// The heavy lifting lives in INP_iOS_Controller.mm. This file only exists so
// the static library has a non-Obj-C compile unit, which helps catch header
// hygiene problems early (no accidental Obj-C leakage into the C++ world).

#include "INP_iOS_Bridge.h"

// Compile-time checks that the bridge header is valid C++.
static_assert(sizeof(mcle_ios_pad_state) > 0, "mcle_ios_pad_state is empty");

// F3-real-input shims into upstream C_4JInput. Each returns -1000..1000
// for stick axes and 0..1000 for triggers; 4J_Input.h scales by /1000
// to recover the -1..1 / 0..1 range upstream gameplay code expects.
// Y axis is FLIPPED on stick reads because GameController.framework's
// y-positive-up convention is opposite of upstream's y-positive-down.

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
    return clamp_axis(-s.ly); // flip: GC y-up -> upstream y-down
}
extern "C" int mcle_ios_input_poll_rx(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    return clamp_axis(s.rx);
}
extern "C" int mcle_ios_input_poll_ry(int pad) {
    mcle_ios_pad_state s = {};
    if (!mcle_ios_input_poll(pad, &s)) return 0;
    return clamp_axis(-s.ry);
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
