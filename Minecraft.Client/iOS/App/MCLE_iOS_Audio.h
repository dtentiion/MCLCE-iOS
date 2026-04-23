// Tiny AVAudioPlayer wrapper for menu music. Looks up a file in the
// app's Documents directory and plays it on loop. Separate from the
// Ruffle SWF pipeline.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 on success (playing or already playing), 0 if no music
// file was found in Documents, -1 if AVAudioPlayer init failed,
// -2 if play() returned NO.
int  mcle_audio_start_menu_music(void);

void mcle_audio_stop_menu_music(void);

// Human-readable status string for the on-screen overlay. NUL-terminated
// UTF-8 copied into `out`, up to `cap` bytes. Returns bytes written
// excluding NUL.
size_t mcle_audio_status(char* out, size_t cap);

// Preload the six UI SFX ogg files from Documents/UI/<name>.ogg.
// Names match console: "back", "craft", "craftfail", "focus", "press",
// "scroll" (see Common/Audio/SoundNames.cpp wchUISoundNames[]).
// Missing files are silently skipped so the app still runs without
// sound assets dropped in.
void   mcle_audio_load_ui_sfx(void);

// Play a preloaded UI SFX by name. volume 0.0-1.0, pitch 1.0 is native.
// Mirrors UIController::PlayUISFX: consecutive calls within 10 ms are
// deduped so toggling a row of checkboxes doesn't produce a horrible
// pile of clicks.
void   mcle_audio_play_ui_sfx(const char* name, float volume, float pitch);

#ifdef __cplusplus
}
#endif
