// The audio session. The music, the game's sounds and the menus' are all
// the game's SoundEngine (Game/MCLESound.cpp).

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Sets the audio session up for the sound engine. 1 when it's active, -1
// when it couldn't be.
int  mcle_audio_init(void);

// Human-readable status string for the on-screen overlay. NUL-terminated
// UTF-8 copied into `out`, up to `cap` bytes. Returns bytes written
// excluding NUL.
size_t mcle_audio_status(char* out, size_t cap);

// Play a UI SFX by name: "back", "craft", "craftfail", "focus", "press",
// "scroll" (see Common/Audio/SoundNames.cpp wchUISoundNames[]). volume
// 0.0-1.0, pitch 1.0 is native. It's UIController::PlayUISFX's
// soundEngine->playUI, in Game/MCLESound.cpp, and like PlayUISFX
// consecutive calls within 10 ms are deduped so toggling a row of
// checkboxes doesn't produce a horrible pile of clicks.
void   mcle_audio_play_ui_sfx(const char* name, float volume, float pitch);

#ifdef __cplusplus
}
#endif
