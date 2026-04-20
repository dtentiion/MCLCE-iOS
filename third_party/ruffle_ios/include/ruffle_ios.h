// C ABI exposed by the ruffle_ios Rust crate.
// Keep in sync with src/lib.rs.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle.
int  ruffle_ios_init(void);
void ruffle_ios_shutdown(void);

// Asset loading.
int  ruffle_ios_load_swf(const char* path);

// Per-frame advance + draw.
void ruffle_ios_tick(float dt_seconds, int vp_w, int vp_h);

// Diagnostic probe: returns a magic constant (0x52554646 == "RUFF") so the
// iOS app can confirm the Rust library actually linked in.
int  ruffle_ios_magic(void);

// Quick SWF-parse test using Ruffle's `swf` crate. Returns the declared
// SWF version on success, or a negative value on failure:
//   -1 = bad input
//   -2 = parse failed (Ruffle rejected the bytes)
int  ruffle_ios_swf_probe(const uint8_t* data, size_t len);

// Probe that ruffle_render is linked. Returns the enum value for
// StageQuality::High (stable). Any positive value means the crate is in.
int  ruffle_ios_render_probe(void);

// Probe that wgpu can initialize on iOS Metal: creates Instance + Adapter
// + Device. Returns 1 on full success, -1 for no adapter, -2 for no
// device.
int  ruffle_ios_wgpu_probe(void);

// Full player lifecycle backed by ruffle_core.
typedef struct PlayerHandle PlayerHandle;

PlayerHandle* ruffle_ios_player_create(int vp_w, int vp_h);
PlayerHandle* ruffle_ios_player_create_with_swf(int vp_w, int vp_h,
                                                const uint8_t* data, size_t len);
void          ruffle_ios_player_destroy(PlayerHandle* h);
void          ruffle_ios_player_tick(PlayerHandle* h, float dt_seconds);

// Diagnostics.
int           ruffle_ios_player_framerate_mHz(PlayerHandle* h);

#ifdef __cplusplus
}
#endif
