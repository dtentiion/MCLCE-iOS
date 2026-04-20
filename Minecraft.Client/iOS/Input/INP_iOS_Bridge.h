#pragma once

#include <stdint.h>

// Lightweight pollable snapshot of whatever controllers GameController.framework
// currently reports. Matches the 4J / Xbox 360 button bitmask in 4J_Input.h so
// the upstream gameplay code can consume it with minimal translation.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcle_ios_pad_state {
    // Bitfield of _360_JOY_BUTTON_* flags from 4J_Input.h.
    uint32_t buttons;

    // Analog sticks, range [-1.0, 1.0]. Y is up-positive.
    float lx, ly;
    float rx, ry;

    // Analog triggers, range [0.0, 1.0].
    float lt, rt;

    // Non-zero if a controller is currently connected and reporting.
    uint8_t connected;
} mcle_ios_pad_state;

// Start listening for controller connect/disconnect. Safe to call repeatedly.
// Must be called from the main thread (GameController notifications require it).
void mcle_ios_input_init(void);

// Stop listening. Optional; called automatically on app teardown.
void mcle_ios_input_shutdown(void);

// Fill `out` with the current state of pad `index` (0 = primary).
// Returns 0 if no controller is present for that index, 1 otherwise.
int mcle_ios_input_poll(int index, mcle_ios_pad_state* out);

#ifdef __cplusplus
}
#endif
