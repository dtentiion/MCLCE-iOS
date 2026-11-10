// iOS audio backend. Uses miniaudio (same library the console LCE uses in
// SoundEngine.cpp), and its implementation is compiled here for the game's
// SoundEngine, which plays everything.
//
// miniaudio supports ogg/mp3/wav/flac out of the box, handles iOS
// AVAudioSession internally, and is a single-header library
// (third_party/miniaudio/miniaudio.h).

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#include "MCLE_iOS_Audio.h"

// Include stb_vorbis BEFORE miniaudio's implementation. miniaudio's
// OGG Vorbis path is enabled only when stb_vorbis is visible at
// implementation compile time; without this, ma_sound_init_from_file
// on a .ogg fails with MA_INVALID_FILE (-10). Matches how console
// LCE's SoundEngine.cpp wires the two together.
#include "stb_vorbis.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <mutex>
#include <string>

namespace {

std::string g_status = "not started";
std::mutex g_mu;

} // namespace

extern "C" int mcle_audio_init(void) {
    // Route audio to playback category so it doesn't duck for silent
    // switch / lock screen music. miniaudio handles the actual Audio
    // Unit setup, but the session category is ours to pick.
    NSError* sessionErr = nil;
    [[AVAudioSession sharedInstance]
        setCategory:AVAudioSessionCategoryPlayback
              error:&sessionErr];
    if (sessionErr) {
        NSLog(@"[mcle_audio] AVAudioSession setCategory err=%@", sessionErr);
    }
    [[AVAudioSession sharedInstance] setActive:YES error:nil];

    std::lock_guard<std::mutex> lk(g_mu);
    g_status = sessionErr ? "session category not set" : "session active";
    return sessionErr ? -1 : 1;
}

extern "C" size_t mcle_audio_status(char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    size_t n = g_status.size();
    if (n > cap - 1) n = cap - 1;
    memcpy(out, g_status.data(), n);
    out[n] = 0;
    return n;
}
