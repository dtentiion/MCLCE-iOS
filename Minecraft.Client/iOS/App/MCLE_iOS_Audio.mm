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
std::string g_status = "not started";
std::vector<std::string> g_tracks;
// Mirrors SoundEngine::m_bHeardTrackA on console: true means the
// track at that index has played recently. GetRandomishTrack picks
// from unheard tracks first, resets the whole array once they're
// all flagged.
std::vector<bool> g_heard;
std::mutex g_mu;
std::atomic<bool> g_shuffle_stop{false};

// Overworld track base names. On console these are indices
// eStream_Overworld_Calm1..eStream_Overworld_piano3 (SoundEngine.h:17-41)
// and SoundEngine::getMusicID returns one at random from this range
// both on the main menu (pMinecraft==nullptr branch at
// SoundEngine.cpp:803) and during overworld gameplay. Menu tracks
// (menu1..menu4) are part of the same pool, not a separate category.
static NSArray<NSString*>* overworldTrackBaseNames() {
    return @[
        @"calm1", @"calm2", @"calm3",
        @"hal1",  @"hal2",  @"hal3", @"hal4",
        @"nuance1", @"nuance2",
        @"creative1", @"creative2", @"creative3",
        @"creative4", @"creative5", @"creative6",
        @"menu1", @"menu2", @"menu3", @"menu4",
        @"piano1", @"piano2", @"piano3",
    ];
}

std::vector<std::string> findAllMenuTracks() {
    std::vector<std::string> out;
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (!docs) return out;
    NSFileManager* fm = [NSFileManager defaultManager];
    NSArray<NSString*>* extsAllowed = @[@"ogg", @"mp3", @"m4a", @"aac",
                                         @"wav", @"aiff", @"caf", @"flac"];
    NSSet<NSString*>* overworldSet =
        [NSSet setWithArray:overworldTrackBaseNames()];
    NSArray<NSString*>* entries = [fm contentsOfDirectoryAtPath:docs error:nil];
    for (NSString* e in entries) {
        NSString* ext = e.pathExtension.lowercaseString;
        if (![extsAllowed containsObject:ext]) continue;
        NSString* base = [e stringByDeletingPathExtension].lowercaseString;
        if (![overworldSet containsObject:base]) continue;
        out.push_back([docs stringByAppendingPathComponent:e].UTF8String);
    }
    return out;
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

    // Mirror SoundEngine::GetRandomishTrack (SoundEngine.cpp:744).
    // Reset the heard-array when all tracks have been played, then
    // pick a random track, preferring ones not flagged heard. Gives
    // up after (range/2) tries and accepts the current pick even if
    // heard - same as console.
    if (g_heard.size() != g_tracks.size()) {
        g_heard.assign(g_tracks.size(), false);
    }
    bool allHeard = true;
    for (bool b : g_heard) { if (!b) { allHeard = false; break; } }
    if (allHeard) {
        std::fill(g_heard.begin(), g_heard.end(), false);
    }
    size_t idx = 0;
    size_t maxTries = (g_tracks.size() + 1) / 2;
    for (size_t tries = 0; tries <= maxTries; ++tries) {
        idx = arc4random_uniform((uint32_t)g_tracks.size());
        if (!g_heard[idx]) break;
    }
    g_heard[idx] = true;
    std::string next = g_tracks[idx];

    ma_result r = ma_sound_init_from_file(
        &g_engine, next.c_str(), 0, NULL, NULL, &g_sound);
    if (r != MA_SUCCESS) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "ma_sound_init failed (%d) for %s", r,
                 [[NSString stringWithUTF8String:next.c_str()] lastPathComponent].UTF8String);
        g_status = buf;
        NSLog(@"[mcle_audio] %s", buf);
        return;
    }
    g_sound_loaded = true;
    g_current_path = next;
    ma_sound_set_end_callback(&g_sound, onSoundEnd, NULL);
    ma_sound_set_volume(&g_sound, 1.0f);
    if (ma_sound_start(&g_sound) != MA_SUCCESS) {
        g_status = std::string("ma_sound_start failed for ") + next;
        NSLog(@"[mcle_audio] %s", g_status.c_str());
        return;
    }
    {
        NSString* last = [[NSString stringWithUTF8String:next.c_str()] lastPathComponent];
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "playing %s (%zu tracks)", last.UTF8String, g_tracks.size());
        g_status = buf;
    }
    NSLog(@"[mcle_audio] %s", g_status.c_str());
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
            char buf[128];
            snprintf(buf, sizeof(buf), "ma_engine_init failed (%d)", r);
            g_status = buf;
            NSLog(@"[mcle_audio] %s", buf);
            return -1;
        }
        g_engine_ok = true;
    }

    std::vector<std::string> tracks = findAllMenuTracks();
    if (tracks.empty()) {
        g_status = "no overworld tracks found in Documents (expected calm/hal/nuance/creative/menu/piano .ogg)";
        NSLog(@"[mcle_audio] %s", g_status.c_str());
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

extern "C" size_t mcle_audio_status(char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    size_t n = g_status.size();
    if (n > cap - 1) n = cap - 1;
    memcpy(out, g_status.data(), n);
    out[n] = 0;
    return n;
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
