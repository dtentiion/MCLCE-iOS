#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

#include "INP_iOS_Bridge.h"
#include "4J_Input.h"  // for _360_JOY_BUTTON_* macros

#include <atomic>
#include <mutex>

namespace {

// Most recent snapshot for up to 4 pads. Updated from the main thread via
// GameController callbacks, read from anywhere via mcle_ios_input_poll().
constexpr int kMaxPads = 4;
std::mutex g_mu;
mcle_ios_pad_state g_state[kMaxPads] = {};

id g_connectObserver = nil;
id g_disconnectObserver = nil;

// Convert a GCExtendedGamepad snapshot to our bitmask + axes.
// Safe to call on any thread; result is copied into g_state under lock.
mcle_ios_pad_state snapshot(GCExtendedGamepad* p) {
    mcle_ios_pad_state s = {};
    s.connected = 1;

    if (p.buttonA.pressed) s.buttons |= _360_JOY_BUTTON_A;
    if (p.buttonB.pressed) s.buttons |= _360_JOY_BUTTON_B;
    if (p.buttonX.pressed) s.buttons |= _360_JOY_BUTTON_X;
    if (p.buttonY.pressed) s.buttons |= _360_JOY_BUTTON_Y;

    if (p.leftShoulder.pressed)  s.buttons |= _360_JOY_BUTTON_LB;
    if (p.rightShoulder.pressed) s.buttons |= _360_JOY_BUTTON_RB;

    // Menu / options (iOS 13+). Fall back quietly if missing.
    if (@available(iOS 13.0, *)) {
        if (p.buttonMenu.pressed)    s.buttons |= _360_JOY_BUTTON_START;
        if (p.buttonOptions.pressed) s.buttons |= _360_JOY_BUTTON_BACK;
        if (p.leftThumbstickButton.pressed)  s.buttons |= _360_JOY_BUTTON_LTHUMB;
        if (p.rightThumbstickButton.pressed) s.buttons |= _360_JOY_BUTTON_RTHUMB;
    }

    if (p.dpad.up.pressed)    s.buttons |= _360_JOY_BUTTON_DPAD_UP;
    if (p.dpad.down.pressed)  s.buttons |= _360_JOY_BUTTON_DPAD_DOWN;
    if (p.dpad.left.pressed)  s.buttons |= _360_JOY_BUTTON_DPAD_LEFT;
    if (p.dpad.right.pressed) s.buttons |= _360_JOY_BUTTON_DPAD_RIGHT;

    s.lx = p.leftThumbstick.xAxis.value;
    s.ly = p.leftThumbstick.yAxis.value;
    s.rx = p.rightThumbstick.xAxis.value;
    s.ry = p.rightThumbstick.yAxis.value;
    s.lt = p.leftTrigger.value;
    s.rt = p.rightTrigger.value;

    // Synthesize digital stick buttons for games that only look at the bitmask.
    const float kStickThresh = 0.5f;
    if (s.lx >  kStickThresh) s.buttons |= _360_JOY_BUTTON_LSTICK_RIGHT;
    if (s.lx < -kStickThresh) s.buttons |= _360_JOY_BUTTON_LSTICK_LEFT;
    if (s.ly >  kStickThresh) s.buttons |= _360_JOY_BUTTON_LSTICK_UP;
    if (s.ly < -kStickThresh) s.buttons |= _360_JOY_BUTTON_LSTICK_DOWN;
    if (s.rx >  kStickThresh) s.buttons |= _360_JOY_BUTTON_RSTICK_RIGHT;
    if (s.rx < -kStickThresh) s.buttons |= _360_JOY_BUTTON_RSTICK_LEFT;
    if (s.ry >  kStickThresh) s.buttons |= _360_JOY_BUTTON_RSTICK_UP;
    if (s.ry < -kStickThresh) s.buttons |= _360_JOY_BUTTON_RSTICK_DOWN;

    const float kTrigThresh = 0.25f;
    if (s.lt > kTrigThresh) s.buttons |= _360_JOY_BUTTON_LT;
    if (s.rt > kTrigThresh) s.buttons |= _360_JOY_BUTTON_RT;

    return s;
}

void hook_pad(GCController* controller, int slot) {
    if (slot < 0 || slot >= kMaxPads) return;

    GCExtendedGamepad* pad = controller.extendedGamepad;
    if (!pad) return;

    pad.valueChangedHandler = ^(GCExtendedGamepad* gp, GCControllerElement* _Nonnull) {
        mcle_ios_pad_state s = snapshot(gp);
        std::lock_guard<std::mutex> lk(g_mu);
        g_state[slot] = s;
    };
}

void refresh_all(void) {
    NSArray<GCController*>* pads = GCController.controllers;
    for (int i = 0; i < kMaxPads; ++i) {
        if (i < (int)pads.count) {
            hook_pad(pads[i], i);
        } else {
            std::lock_guard<std::mutex> lk(g_mu);
            g_state[i] = {};  // clear slot
        }
    }
}

} // namespace

extern "C" void mcle_ios_input_init(void) {
    static std::atomic<bool> initialized{false};
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true)) return;

    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    g_connectObserver = [nc addObserverForName:GCControllerDidConnectNotification
                                        object:nil
                                         queue:NSOperationQueue.mainQueue
                                    usingBlock:^(NSNotification* _Nonnull) {
        refresh_all();
    }];
    g_disconnectObserver = [nc addObserverForName:GCControllerDidDisconnectNotification
                                           object:nil
                                            queue:NSOperationQueue.mainQueue
                                       usingBlock:^(NSNotification* _Nonnull) {
        refresh_all();
    }];

    // Pick up anything already paired at launch.
    refresh_all();
}

extern "C" void mcle_ios_input_shutdown(void) {
    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    if (g_connectObserver)    { [nc removeObserver:g_connectObserver];    g_connectObserver = nil; }
    if (g_disconnectObserver) { [nc removeObserver:g_disconnectObserver]; g_disconnectObserver = nil; }
}

extern "C" int mcle_ios_input_poll(int index, mcle_ios_pad_state* out) {
    if (!out || index < 0 || index >= kMaxPads) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    *out = g_state[index];
    return g_state[index].connected ? 1 : 0;
}
