// The menus' own sounds. The music and the game's sounds are the game's
// SoundEngine (Game/MCLESound.cpp).

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Starts the engine the menus' sounds play on. 1 when it's up, -1 when
// it couldn't be made.
int  mcle_audio_init(void);

// Human-readable status string for the on-screen overlay. NUL-terminated
// UTF-8 copied into `out`, up to `cap` bytes. Returns bytes written
// excluding NUL.
size_t mcle_audio_status(char* out, size_t cap);

// Preload the six UI SFX ogg files from Documents/UI/<name>.ogg.
// The UI sound names: "back", "craft", "craftfail", "focus", "press",
// "scroll" (see Common/Audio/SoundNames.cpp wchUISoundNames[]).
// Missing files are silently skipped so the app still runs without
// sound assets dropped in.
void   mcle_audio_load_ui_sfx(void);

// Play a preloaded UI SFX by name. volume 0.0-1.0, pitch 1.0 is native.
// Mirrors UIController::PlayUISFX: consecutive calls within 10 ms are
// deduped so toggling a row of checkboxes doesn't produce a horrible
// pile of clicks.
void   mcle_audio_play_ui_sfx(const char* name, float volume, float pitch);

// The UI SFX volume, 0..100, the scale of eGameSetting_SoundFXVolume
void   mcle_audio_set_sfx_volume(int volume_0_100);

#ifdef __cplusplus
}
#endif
