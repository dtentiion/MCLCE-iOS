#pragma once

// Minimal C bridge the Obj-C app layer uses to bring up GameSWF. Keeps
// C++/Obj-C details out of the AppDelegate, and lets us hot-swap the SWF
// runtime later without touching UIKit code.

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the SWF runtime: create our Metal render_handler, install it,
// and allocate a player. Safe to call multiple times (subsequent calls are
// no-ops). Returns 0 on success, non-zero on failure.
int mcle_swf_init(void);

// Tear down the SWF runtime. Safe to call before shutdown.
void mcle_swf_shutdown(void);

// True if mcle_swf_init succeeded at some point. Useful for gating feature
// code that depends on the runtime being live.
int  mcle_swf_is_ready(void);

// Advance the SWF player by `dt` seconds and draw its current frame into
// the active Metal render pass. Safe to call even when no movie is loaded
// (no-op in that case). Must be called from inside a mcle_metal_frame_begin
// / _end bracket.
void mcle_swf_tick(float dt);

// Attempt to load a SWF from `path` (bundle-relative or absolute) and set
// it as the active root movie. Returns 0 on success, non-zero on failure.
int  mcle_swf_load(const char* path);

// Draws a synthetic colored rectangle via the render_handler, bypassing
// the SWF player. Used before a real movie is loaded so the render_handler
// + Metal pipeline are visibly exercised on device.
void mcle_swf_draw_test_rect(int vp_width, int vp_height);

#ifdef __cplusplus
}
#endif
