// iOS audio backend for the menus' own sounds. Uses miniaudio (same
// library the console LCE uses in SoundEngine.cpp), and its implementation
// is compiled here for the game's SoundEngine too. The music is that
// engine's.
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

#include <atomic>
#include <mutex>
#include <string>

namespace {

ma_engine g_engine;
bool g_engine_ok = false;
std::string g_status = "not started";
std::mutex g_mu;

// The UI SFX volume from MCLE_SETTING_SoundFXVolume, 0..1
std::atomic<float> g_sfx_volume{1.0f};

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
    if (!g_engine_ok) {
        ma_engine_config cfg = ma_engine_config_init();
        ma_result r = ma_engine_init(&cfg, &g_engine);
        if (r != MA_SUCCESS) {
            char buf[128];
            snprintf(buf, sizeof(buf), "ma_engine_init failed (%d)", r);
            g_status = buf;
            NSLog(@"[mcle_audio] %s", buf);
            return -1;
        }
        g_engine_ok = true;
        g_status = "engine up";
    }
    return 1;
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

// --- UI SFX (back, press, focus, scroll, craft, craftfail) --------------
//
// Preloaded at app launch and held in g_ui_sounds[]. Names match
// console: see Common/Audio/SoundNames.cpp wchUISoundNames[]. Each
// file lives at Documents/UI/<name>.ogg. The console SoundEngine also
// rate-limits simultaneous UI SFX to one per 10 ms window
// (Common/UI/UIController.cpp:2529) to stop checkbox cascades etc
// from blasting audio; the same limit applies here.
namespace {
static const char* const g_ui_names[] = {
    "back", "craft", "craftfail", "focus", "press", "scroll",
};
static constexpr size_t kNumUiSfx = 6;
ma_sound g_ui_sounds[kNumUiSfx];
bool     g_ui_loaded[kNumUiSfx] = {};
std::atomic<uint64_t> g_ui_last_play_ms{0};

uint64_t now_ms() {
    return (uint64_t)([[NSDate date] timeIntervalSince1970] * 1000.0);
}
}

extern "C" void mcle_audio_load_ui_sfx(void) {
    if (!g_engine_ok) return;
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (!docs.length) return;
    for (size_t i = 0; i < kNumUiSfx; ++i) {
        if (g_ui_loaded[i]) continue;
        NSString* path = [docs stringByAppendingPathComponent:
            [NSString stringWithFormat:@"UI/%s.ogg", g_ui_names[i]]];
        // Else where Windows64 has them
        if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
            path = [docs stringByAppendingPathComponent:
                [NSString stringWithFormat:@"Windows64Media/Sound/Minecraft/UI/%s.ogg", g_ui_names[i]]];
        }
        if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
            NSLog(@"[audio] UI sfx missing: %@", path);
            continue;
        }
        ma_result r = ma_sound_init_from_file(
            &g_engine, path.UTF8String,
            MA_SOUND_FLAG_DECODE, NULL, NULL, &g_ui_sounds[i]);
        if (r == MA_SUCCESS) {
            g_ui_loaded[i] = true;
            NSLog(@"[audio] UI sfx loaded: %s", g_ui_names[i]);
        } else {
            NSLog(@"[audio] UI sfx load failed '%s' rc=%d",
                  g_ui_names[i], (int)r);
        }
    }
}

extern "C" void mcle_audio_set_sfx_volume(int volume_0_100) {
    int v = volume_0_100;
    if (v < 0) v = 0; if (v > 100) v = 100;
    g_sfx_volume.store((float)v / 100.0f);
}

extern "C" void mcle_audio_play_ui_sfx(const char* name, float volume, float pitch) {
    if (!g_engine_ok || !name) return;
    uint64_t now = now_ms();
    uint64_t last = g_ui_last_play_ms.load(std::memory_order_relaxed);
    if (now - last < 10) return;
    g_ui_last_play_ms.store(now, std::memory_order_relaxed);

    int idx = -1;
    for (size_t i = 0; i < kNumUiSfx; ++i) {
        if (strcmp(name, g_ui_names[i]) == 0) { idx = (int)i; break; }
    }
    if (idx < 0 || !g_ui_loaded[idx]) return;

    ma_sound* s = &g_ui_sounds[idx];
    // Scale caller's volume by the user's settings.dat value so the
    // Sound slider on SettingsAudioMenu actually affects UI SFX.
    float effective = volume * g_sfx_volume.load();
    ma_sound_set_volume(s, effective);
    ma_sound_set_pitch(s, pitch);
    ma_sound_seek_to_pcm_frame(s, 0);
    ma_sound_start(s);
}
