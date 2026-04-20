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

// Same as mcle_swf_tick, but passes the current drawable size so the player
// can set its display viewport. Without a non-zero viewport the root clips
// everything and nothing visible is drawn.
void mcle_swf_tick_with_viewport(float dt, int vp_w, int vp_h);

// Attempt to load a SWF from `path` (bundle-relative or absolute) and set
// it as the active root movie. Returns 0 on success, non-zero on failure.
int  mcle_swf_load(const char* path);

// Draws a synthetic colored rectangle via the render_handler, bypassing
// the SWF player. Used before a real movie is loaded so the render_handler
// + Metal pipeline are visibly exercised on device.
void mcle_swf_draw_test_rect(int vp_width, int vp_height);

// True if a movie has been loaded and set as the active root.
int  mcle_swf_has_movie(void);

// Returns a short human-readable status string reflecting the most recent
// operation (init, load, tick). Stable pointer; owned by the bridge.
// Useful for on-screen diagnostics when Mac-side device logs are not
// available.
const char* mcle_swf_last_status(void);

// Recent GameSWF log messages (newline separated). Same lifetime rules as
// mcle_swf_last_status.
const char* mcle_swf_gameswf_log(void);

#ifdef __cplusplus
}
#endif
