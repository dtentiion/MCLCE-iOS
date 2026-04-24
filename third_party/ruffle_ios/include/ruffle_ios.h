// C ABI exposed by the ruffle_ios Rust crate.
// Keep in sync with src/lib.rs.

#pragma once

#include <stdint.h>
#include <stddef.h>

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

// Probe that a wgpu Surface can be created from a CAMetalLayer. Pass the
// CAMetalLayer* cast to void*. Returns 1 on full success, <0 on failure
// (see lib.rs for specific codes).
int  ruffle_ios_surface_probe(void* ca_metal_layer);

// Full player lifecycle backed by ruffle_core.
typedef struct PlayerHandle PlayerHandle;

PlayerHandle* ruffle_ios_player_create(int vp_w, int vp_h);
PlayerHandle* ruffle_ios_player_create_with_swf(int vp_w, int vp_h,
                                                const uint8_t* data, size_t len);
// Preferred: wgpu-backed player drawing directly into the CAMetalLayer.
// `base_path` (UTF-8, NUL-terminated; may be NULL) is where the SWF's
// relative Loader fetches resolve. Pass the iOS Documents directory so
// sister SWFs the movie references (skinHD.swf, etc.) are found.
PlayerHandle* ruffle_ios_player_create_wgpu(void* ca_metal_layer,
                                            int vp_w, int vp_h,
                                            const uint8_t* data, size_t len,
                                            const char* base_path);
void          ruffle_ios_player_destroy(PlayerHandle* h);
void          ruffle_ios_player_tick(PlayerHandle* h, float dt_seconds);

// Advance one simulated frame WITHOUT rendering. Keeps whatever was
// last presented on the wgpu surface. Used for scene transitions so
// a new root movie can construct + be Init'd before the first
// visible frame, matching console's atomic scene-load semantics.
void          ruffle_ios_player_tick_headless(PlayerHandle* h, float dt_seconds);

// Same as tick_headless, but snapshots every XUI-origin Bitmap's
// matrix before the tick and restores it after. Prevents the panorama
// / logo / tooltips from appearing to jump during a 30-tick transition
// burst while still letting everything else (new scene init chain,
// button wiring) advance normally.
void          ruffle_ios_player_tick_headless_preserve_xui(
                  PlayerHandle* h, float dt_seconds);

// Stash / restore matrices for every XUI-origin Bitmap in one shot.
// Use around a full scene transition (replace_swf + headless burst +
// button init) so the panorama stays visually locked across both
// the ticks and the follow-up init calls that also advance scroll.
void          ruffle_ios_player_snapshot_xui_matrices(PlayerHandle* h);
void          ruffle_ios_player_restore_xui_matrices(PlayerHandle* h);

// Play/stop Timelines on every non-root stage child. Used around the
// 30-tick transition burst: stop before, play after. Root clip
// (depth 0) keeps advancing so the new scene constructs normally.
void          ruffle_ios_player_set_xui_siblings_playing(
                  PlayerHandle* h, int playing);

// Diagnostics.
int           ruffle_ios_player_framerate_mHz(PlayerHandle* h);

// Copy the most recent ExternalInterface call log (newline-separated,
// NUL-terminated) into `out`, up to `cap` bytes. Returns the number of
// bytes written excluding the NUL.
size_t        ruffle_ios_extint_log(uint8_t* out, size_t cap);

// AS3 trace() output and AVM warnings captured from Ruffle's LogBackend.
// Same format as ruffle_ios_extint_log.
size_t        ruffle_ios_avm_log(uint8_t* out, size_t cap);

// Current frame index the player is rendering. -1 if no movie.
int           ruffle_ios_player_current_frame(PlayerHandle* h);
int           ruffle_ios_player_movie_width(PlayerHandle* h);
int           ruffle_ios_player_movie_height(PlayerHandle* h);

// Monotonic count of how many times ruffle_ios_player_tick was called.
uint64_t      ruffle_ios_tick_count(void);

// Stage a TTF/OTF font to be registered with the *next* player created via
// ruffle_ios_player_create_wgpu, BEFORE it runs preload. This is what you
// want for device fonts the SWF references at init time (the menu font):
// registering after the player is built misses the first text layout and
// glyphs render as empty boxes. Same argument shape as the post-create
// register function below. Returns 1 on success, 0 on bad args,
// -1 if the staging list's lock is poisoned.
int           ruffle_ios_stage_device_font(const uint8_t* name, size_t name_len,
                                           const uint8_t* data, size_t data_len,
                                           int is_bold, int is_italic);

// Register a TTF/OTF font as a Ruffle device font. The SWF can then look it
// up by `name` when it asks for a device font. Returns 1 on success, 0 on
// bad arguments, -1 on a player lock failure.
//   h         : the PlayerHandle
//   name      : font name as the SWF will request it (e.g. "Mojangles7")
//   name_len  : byte length of name (not NUL-terminated)
//   data      : pointer to TTF/OTF bytes
//   data_len  : length of data
//   is_bold   : 0 or 1
//   is_italic : 0 or 1
int           ruffle_ios_register_device_font(PlayerHandle* h,
                                              const uint8_t* name, size_t name_len,
                                              const uint8_t* data, size_t data_len,
                                              int is_bold, int is_italic);

// Controller input forwarded to the Ruffle player. The button code uses
// this iOS-side mapping (matches Ruffle's GamepadButton declaration order):
//   0 South / A,  1 East / B,  2 North / Y,  3 West / X,
//   4 Start,      5 Select,
//   6 DPadUp,     7 DPadDown,  8 DPadLeft,   9 DPadRight,
//   10 LeftTrigger,   11 RightTrigger,
//   12 LeftTrigger2,  13 RightTrigger2.
void          ruffle_ios_player_gamepad_down(PlayerHandle* h, int code);
void          ruffle_ios_player_gamepad_up(PlayerHandle* h, int code);

// Tick-stage breakdown. out_counters must point to an array of at least 4
// u64: [lock_ok, after_tick, after_run_frame, after_render]. A fifth slot,
// if provided (len >= 5), receives the per-handle executor_runs count.
// `is_playing` receives the most recent Player::is_playing() sample:
// 0=unknown, 1=true, 2=false. `handle_ptr` is the PlayerHandle whose
// executor_runs to read (NULL returns 0). Any pointer may be NULL.
void          ruffle_ios_player_diag(uint64_t* out_counters, size_t len,
                                     int* is_playing,
                                     PlayerHandle* handle_ptr);

// Frame-transition diag: the last cur_frame samples taken around
// Player::tick and Player::run_frame, plus the number of ticks where the
// root clip's frame actually changed. Any pointer may be NULL.
void          ruffle_ios_player_frame_diag(int* cf_pre, int* cf_mid,
                                           int* cf_post, uint64_t* frame_advances);

// Burn-frames diag: summary of the back-to-back run_frame loop performed
// once at player creation. `done` is 1 if the burn ran (0 otherwise).
// `first` is cur_frame before the loop; `final_cf` is after the last call;
// `max_cf` is the highest value seen; `unique_vals` is the count of
// distinct frame values observed (1 = clip never moved).
void          ruffle_ios_burn_diag(int* done, int* first, int* final_cf,
                                   int* max_cf, int* unique_vals);

// Count of AS3 trace() calls and AVM warnings. If `traces` stays flat as
// ticks climb, AS3 is not executing past whatever ran at startup, which
// means a deferred init / event handler never fired. Either pointer may be
// NULL.
void          ruffle_ios_avm_counts(uint64_t* traces, uint64_t* warns);

// Fill `out` (UTF-8, NUL-terminated) with `name\tclass\n` for each direct
// child of the root clip. Also pushes the full list to AVM_LOG so it
// appears in crash_log.txt. Returns bytes written (excluding NUL).
size_t        ruffle_ios_enumerate_root_children(PlayerHandle* h,
                                                 uint8_t* out, size_t cap);

// Toggle the `visible` flag on a named direct child of the root clip.
// Used to hide `iggy_Splash`, 4J's loading placeholder that the console
// engine hides once a scene is ready but our port leaves as a gray
// strip. Returns 1 if found and updated, 0 on bad args or not found,
// -1 on player lock failure.
int           ruffle_ios_set_root_child_visible(PlayerHandle* h,
                                                const uint8_t* name, size_t name_len,
                                                int visible);

// Instantiate an AS3 class by name and attach its DisplayObject as a
// direct child of the root clip at the given depth. Used for things
// like dropping the menu Panorama under the main menu buttons without
// editing the MainMenu SWF. Returns 1 ok, 0 bad args, -1 lock fail,
// -2 resolution failure (see AVM_LOG).
int           ruffle_ios_instantiate_class_on_root(PlayerHandle* h,
                                                   const uint8_t* class_name, size_t class_name_len,
                                                   int depth);

// Register a PNG as a Bitmap character keyed by an AS3 class name.
// Ports 4J's Iggy XUI texture-import mechanism: when a SWF places a
// class like `Panorama_Background_S` that exists only as a PNG on
// disc (not an AS3 class), we register it here and the resolver's
// XUI fallback finds it. Returns 1 ok, 0 bad args, -1 lock fail,
// -2 decode / register failure.
int           ruffle_ios_register_xui_bitmap(PlayerHandle* h,
                                             const uint8_t* class_name, size_t class_name_len,
                                             const uint8_t* png,        size_t png_len,
                                             float display_scale_x, float display_scale_y);

// Load a sibling SWF (Panorama1080.swf / ComponentLogo1080.swf etc.)
// from bytes and attach as a child of the current root clip at the
// given depth. Matches console Iggy's multi-movie compositing model.
// Panorama typically goes at depth 0 (behind menu), logo at depth
// 100 (in front). Returns 1 ok, 0 bad args, -1 lock fail, -2 parse
// or attach failure.
int           ruffle_ios_add_sibling_swf_to_root(PlayerHandle* h,
                                                 const uint8_t* data, size_t data_len,
                                                 const uint8_t* url,  size_t url_len,
                                                 int depth,
                                                 float scale_x, float scale_y,
                                                 float tx, float ty);

// Swap the root movie on an existing player (used for menu scene
// transitions so the wgpu surface stays alive). url_ptr/url_len may be
// empty to default to "file://mcle.swf". Returns 1 ok, 0 bad args,
// -1 lock failure, -2 parse failure.
int           ruffle_ios_player_replace_swf(PlayerHandle* h,
                                            const uint8_t* data, size_t data_len,
                                            const uint8_t* url,  size_t url_len);

// One level deeper: list `childName`'s own direct children (the inner
// display list of a named root child, e.g. what Button1 actually
// contains). Same output shape as ruffle_ios_enumerate_root_children,
// also pushed to AVM_LOG. Returns bytes written (excluding NUL). 0 is
// returned both on empty containers and on lookup failure; check the
// AVM_LOG line for "<N total>" to tell them apart.
size_t        ruffle_ios_enumerate_named_child_children(PlayerHandle* h,
                                                        const uint8_t* child_name, size_t child_name_len,
                                                        uint8_t* out, size_t cap);

// Recursively enumerate a named root child's subtree up to max_depth
// levels. Output is indented: "<name>\t<class>" per line, depth implied
// by leading spaces (2 per level). Same buffer/return semantics as the
// shallower enumerate_named_child_children.
size_t        ruffle_ios_enumerate_subtree_of(PlayerHandle* h,
                                              const uint8_t* child_name, size_t child_name_len,
                                              size_t max_depth,
                                              uint8_t* out, size_t cap);

// Invoke `childName.methodName(label, id)` on a direct child of the root
// clip, mirroring the console LCE's IggyPlayerCallMethodRS pattern. The
// status string produced by the call is always pushed to AVM_LOG.
// Returns:  1  ok
//           0  bad args (null/empty name, bad UTF-8)
//          -1  could not take player lock
//          -2  call itself failed (AS3 exception, no such child, etc.);
//              see AVM_LOG for the specific status line.
int           ruffle_ios_call_init_on_named_child(PlayerHandle* h,
                                                  const uint8_t* child_name, size_t child_name_len,
                                                  const uint8_t* method_name, size_t method_name_len,
                                                  const uint8_t* label, size_t label_len,
                                                  double id);

// Init(label, id, checked) on an FJ_CheckBox child. The generic
// call_init_on_named_child only knows FJ_Button's 2-arg Init shape
// so checkboxes and sliders stay "Not Initialised!" until these
// control-type-aware variants are used instead.
int           ruffle_ios_call_init_checkbox(PlayerHandle* h,
                                            const uint8_t* child_name, size_t child_name_len,
                                            const uint8_t* label, size_t label_len,
                                            double id, int checked);

// Init(label, id, min, max, current) on an FJ_Slider child.
int           ruffle_ios_call_init_slider(PlayerHandle* h,
                                          const uint8_t* child_name, size_t child_name_len,
                                          const uint8_t* label, size_t label_len,
                                          double id, int min, int max, int current);

// Clear AS3 stage focus. Call on scene exit so the focus highlight
// from the previous scene doesn't bleed into the next.
void          ruffle_ios_clear_focus(PlayerHandle* h);

// Toggle Ruffle's auto-drawn yellow focus rectangle. Pass 1 once
// at startup so the SWF's authored focus outline art (FJ_Slider_
// Outline, FJ_CheckBox_Outline) is the only yellow frame on
// screen. Without this Ruffle paints its own accessibility focus
// highlight on top, producing a duplicate outline.
void          ruffle_ios_suppress_auto_focus_highlight(PlayerHandle* h, int suppress);

// Set AS3 stage focus to a named direct child of the root clip.
// Needed on Settings sub-scenes so keyboard-mapped gamepad input
// routes to a control instead of vanishing into a null-focus
// stage. Returns 1 ok, 0 bad args, -1 lock fail, -2 child not
// found or not InteractiveObject.
int           ruffle_ios_focus_named_child(PlayerHandle* h,
                                           const uint8_t* name, size_t name_len);

// Call SetFocus(id) on the root SWF's document class. Mirrors
// console's UIScene::gainFocus calling the SWF's SetFocus via
// IggyPlayerCallMethodRS with a single number arg. Pass id=-1
// for the auto-focus path (FJ_Document.SetFocus picks the child
// with tabIndex == 1 and fires handleInitFocus via extint).
// Prefer this over ruffle_ios_focus_named_child for scene-entry
// focus so the authored tabIndex decides and the SWF fires its
// init-focus event. Returns 1 ok, 0 bad args, -1 lock fail,
// -2 call failed.
int           ruffle_ios_call_root_set_focus(PlayerHandle* h, double id);

// Generic: call an AS3 method on the root SWF's document class
// with zero or more number args. Matches console's
// IggyPlayerCallMethodRS with an IGGY_DATATYPE_number array
// shape. Used by the MessageBox overlay scene for Init(count,
// focus) and AutoResize().
int           ruffle_ios_call_root_method_numbers(PlayerHandle* h,
                                                  const uint8_t* method, size_t method_len,
                                                  const double* args, size_t args_len);

// --- FJ_ButtonList / FJ_ButtonList_ListIconLeft widget calls ---
// Dynamic list widget used by LoadOrJoinMenu (SavesList/JoinList),
// HowToPlayMenu, LeaderboardMenu, DLCMainMenu, and SkinSelectMenu.
// Matches the console-side UIControl_ButtonList / UIControl_SaveList
// AS3 method contract (UIControl_ButtonList.cpp, UIControl_SaveList.cpp).
// Returns follow the rest of the file: 1 ok, 0 bad args, -1 lock
// fail, -2 AVM2 call err or child not found.

// Init(id) on the named list child. Call once per list on scene
// entry, before any addItem / removeAll.
int           ruffle_ios_call_list_init(PlayerHandle* h,
                                        const uint8_t* child_name, size_t child_name_len,
                                        double id);

// addNewItem(label, data, iconName) on a
// FJ_ButtonList_ListIconLeft child. `data` becomes the item's
// button id reported back through handlePress. Pass iconName
// empty for no icon.
int           ruffle_ios_call_list_add_item(PlayerHandle* h,
                                            const uint8_t* child_name, size_t child_name_len,
                                            const uint8_t* label, size_t label_len,
                                            double data,
                                            const uint8_t* icon_name, size_t icon_name_len);

// addNewItem(label, data) on an FJ_ButtonList_Menu child (2-arg
// no-icon variant used by HowToPlayMenu, LeaderboardMenu, and
// the other plain-label scenes).
int           ruffle_ios_call_list_add_menu_item(PlayerHandle* h,
                                                 const uint8_t* child_name, size_t child_name_len,
                                                 const uint8_t* label, size_t label_len,
                                                 double data);

// removeAllItems() on the named list child. Used before a
// repopulate so stale items don't bleed from a previous scene
// tick.
int           ruffle_ios_call_list_remove_all(PlayerHandle* h,
                                              const uint8_t* child_name, size_t child_name_len);

// Toggle a stage-level sibling SWF (Panorama at -1, Tooltips at
// 100, Logo at 101) on or off. Mirrors console's per-scene
// UILayer::showComponent(eUIComponent_Logo / _Panorama, bool) in
// updateComponents(). Returns 1 if a sibling at the given depth
// was found, 0 if none, -1 on lock fail.
int           ruffle_ios_set_xui_sibling_visible_at_depth(PlayerHandle* h,
                                                          int depth, int visible);

// Register a callback that fires when LCE AS3 reports a setting
// change via ExternalInterface. The two method names currently
// routed are "handleCheckboxToggled" (id=control id, value=1.0 for
// checked / 0.0 for unchecked) and "handleSliderMove" (id=control
// id, value=slider position). The `method` pointer is a NUL-
// terminated UTF-8 string valid only for the duration of the call.
typedef void (*ruffle_ios_settings_event_cb)(const char* method, double id, double value);
void          ruffle_ios_set_settings_event_callback(ruffle_ios_settings_event_cb cb);

#ifdef __cplusplus
}
#endif
