// iOS audio backend. Uses miniaudio (same library the console LCE
// uses in SoundEngine.cpp) so we can play the stock .ogg tracks from
// the console build directly without transcoding. Scans Documents for
// menu1.ogg..menu4.ogg (or anything else supported) and shuffles
// across them on loop.
//
// miniaudio supports ogg/mp3/wav/flac out of the box, handles iOS
// AVAudioSession internally, and is a single-header library
// (third_party/miniaudio/miniaudio.h).

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#include "MCLE_iOS_Audio.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace {

ma_engine g_engine;
bool g_engine_ok = false;
ma_sound g_sound;
bool g_sound_loaded = false;
std::string g_current_path;
std::vector<std::string> g_tracks;
std::mutex g_mu;
std::atomic<bool> g_shuffle_stop{false};

std::vector<std::string> findAllMenuTracks() {
    std::vector<std::string> out;
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (!docs) return out;
    NSFileManager* fm = [NSFileManager defaultManager];
    NSArray<NSString*>* extsAllowed = @[@"ogg", @"mp3", @"m4a", @"aac",
                                         @"wav", @"aiff", @"caf", @"flac"];
    NSArray<NSString*>* preferredPrefixes = @[@"menu", @"calm", @"minecraft",
                                               @"hal", @"piano"];

    NSArray<NSString*>* entries = [fm contentsOfDirectoryAtPath:docs error:nil];
    std::vector<std::string> preferred;
    std::vector<std::string> fallback;
    for (NSString* e in entries) {
        NSString* ext = e.pathExtension.lowercaseString;
        if (![extsAllowed containsObject:ext]) continue;
        NSString* full = [docs stringByAppendingPathComponent:e];
        std::string s = full.UTF8String;
        NSString* lower = e.lowercaseString;
        BOOL isPreferred = NO;
        for (NSString* p in preferredPrefixes) {
            if ([lower hasPrefix:p]) { isPreferred = YES; break; }
        }
        if (isPreferred) preferred.push_back(s);
        else fallback.push_back(s);
    }
    if (!preferred.empty()) return preferred;
    return fallback;
}

// Forward decl.
void startRandomTrack();

// miniaudio end-of-sound callback. Fires on the audio thread; we
// defer the actual next-track selection to the main thread so we
// don't do file I/O inside the callback.
void onSoundEnd(void* pUserData, ma_sound* pSound) {
    (void)pUserData; (void)pSound;
    if (g_shuffle_stop.load()) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        startRandomTrack();
    });
}

void startRandomTrack() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_shuffle_stop.load()) return;
    if (g_tracks.empty() || !g_engine_ok) return;

    // Unload the previous sound if any.
    if (g_sound_loaded) {
        ma_sound_uninit(&g_sound);
        g_sound_loaded = false;
    }

    // Pick a track that isn't the one we just played (when >1).
    std::string next;
    for (int tries = 0; tries < 8; ++tries) {
        uint32_t idx = arc4random_uniform((uint32_t)g_tracks.size());
        const std::string& pick = g_tracks[idx];
        if (g_tracks.size() == 1 || pick != g_current_path) {
            next = pick;
            break;
        }
    }
    if (next.empty()) next = g_tracks.front();

    ma_result r = ma_sound_init_from_file(
        &g_engine, next.c_str(), 0, NULL, NULL, &g_sound);
    if (r != MA_SUCCESS) {
        NSLog(@"[mcle_audio] ma_sound_init_from_file(%s) failed: %d",
              next.c_str(), r);
        return;
    }
    g_sound_loaded = true;
    g_current_path = next;
    ma_sound_set_end_callback(&g_sound, onSoundEnd, NULL);
    ma_sound_set_volume(&g_sound, 1.0f);
    if (ma_sound_start(&g_sound) != MA_SUCCESS) {
        NSLog(@"[mcle_audio] ma_sound_start failed for %s", next.c_str());
        return;
    }
    NSLog(@"[mcle_audio] playing %s (%zu tracks available)",
          [[NSString stringWithUTF8String:next.c_str()] lastPathComponent].UTF8String,
          g_tracks.size());
}

} // namespace

extern "C" int mcle_audio_start_menu_music(void) {
    // Already playing? Nothing to do.
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_sound_loaded && ma_sound_is_playing(&g_sound)) return 1;
    }

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

    // Lazy-init the miniaudio engine on first call.
    if (!g_engine_ok) {
        ma_engine_config cfg = ma_engine_config_init();
        ma_result r = ma_engine_init(&cfg, &g_engine);
        if (r != MA_SUCCESS) {
            NSLog(@"[mcle_audio] ma_engine_init failed: %d", r);
            return -1;
        }
        g_engine_ok = true;
    }

    std::vector<std::string> tracks = findAllMenuTracks();
    if (tracks.empty()) {
        NSLog(@"[mcle_audio] no supported audio files found in Documents");
        return 0;
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_tracks = std::move(tracks);
        g_shuffle_stop.store(false);
    }
    startRandomTrack();
    return g_sound_loaded ? 1 : -2;
}

extern "C" void mcle_audio_stop_menu_music(void) {
    g_shuffle_stop.store(true);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_sound_loaded) {
            ma_sound_stop(&g_sound);
            ma_sound_uninit(&g_sound);
            g_sound_loaded = false;
        }
        g_current_path.clear();
    }
    [[AVAudioSession sharedInstance] setActive:NO
                                   withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                         error:nil];
}
