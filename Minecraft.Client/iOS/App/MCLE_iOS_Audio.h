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

#ifdef __cplusplus
}
#endif
