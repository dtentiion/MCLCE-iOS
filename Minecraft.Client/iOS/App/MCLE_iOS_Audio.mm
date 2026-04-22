// Minimal iOS audio backend. Plays a menu music track via AVAudioPlayer
// on loop. No Ruffle involvement; this runs fully in parallel with the
// SWF renderer.
//
// The player scans the app's Documents directory for common menu-music
// filenames and plays the first one it finds. Users drop any mp3 / m4a
// /  aac / wav into Documents alongside MainMenu1080.swf. OGG isn't
// supported by AVAudioPlayer without a third-party decoder, so the
// user has to transcode (ffmpeg) if the source is .ogg.

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#include "MCLE_iOS_Audio.h"

namespace {
AVAudioPlayer* g_menu_player = nil;

NSString* findMenuMusic(void) {
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (!docs) return nil;
    NSFileManager* fm = [NSFileManager defaultManager];
    // Common filename candidates. First match wins. Keep this short; the
    // point is "drop a music file in Documents and it plays", not a
    // sophisticated asset system.
    NSArray<NSString*>* candidates = @[
        @"menu_music.mp3",
        @"menu_music.m4a",
        @"menu_music.aac",
        @"menu_music.wav",
        @"menu.mp3",
        @"menu.m4a",
        @"calm1.mp3",
        @"calm1.m4a",
        @"minecraft.mp3",
    ];
    for (NSString* name in candidates) {
        NSString* p = [docs stringByAppendingPathComponent:name];
        if ([fm fileExistsAtPath:p]) return p;
    }
    // Fallback: first file in Documents matching an iOS-supported
    // extension. Lets the user drop whatever they have without renaming.
    NSArray<NSString*>* entries = [fm contentsOfDirectoryAtPath:docs error:nil];
    for (NSString* e in entries) {
        NSString* ext = e.pathExtension.lowercaseString;
        if ([@[@"mp3", @"m4a", @"aac", @"wav", @"aiff", @"caf"] containsObject:ext]) {
            return [docs stringByAppendingPathComponent:e];
        }
    }
    return nil;
}
} // namespace

extern "C" int mcle_audio_start_menu_music(void) {
    if (g_menu_player && g_menu_player.playing) return 1;

    // Route audio to playback category so it doesn't duck for silent
    // switch / lock screen music (and mixes with other audio sources).
    NSError* sessionErr = nil;
    [[AVAudioSession sharedInstance]
        setCategory:AVAudioSessionCategoryPlayback
              error:&sessionErr];
    if (sessionErr) {
        NSLog(@"[mcle_audio] AVAudioSession setCategory err=%@", sessionErr);
    }
    [[AVAudioSession sharedInstance] setActive:YES error:nil];

    NSString* path = findMenuMusic();
    if (!path) {
        NSLog(@"[mcle_audio] no menu music file found in Documents");
        return 0;
    }
    NSURL* url = [NSURL fileURLWithPath:path];
    NSError* err = nil;
    AVAudioPlayer* p = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&err];
    if (!p || err) {
        NSLog(@"[mcle_audio] init player failed for %@: %@", path, err);
        return -1;
    }
    p.numberOfLoops = -1;   // infinite
    p.volume = 1.0f;
    [p prepareToPlay];
    if (![p play]) {
        NSLog(@"[mcle_audio] play failed for %@", path);
        return -2;
    }
    g_menu_player = p;
    NSLog(@"[mcle_audio] playing %@", path.lastPathComponent);
    return 1;
}

extern "C" void mcle_audio_stop_menu_music(void) {
    if (g_menu_player) {
        [g_menu_player stop];
        g_menu_player = nil;
    }
    [[AVAudioSession sharedInstance] setActive:NO
                                   withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                         error:nil];
}
