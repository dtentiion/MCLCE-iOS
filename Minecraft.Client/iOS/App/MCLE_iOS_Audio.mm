// Minimal iOS audio backend. Scans the app's Documents directory for
// any supported audio file, picks one at random, plays it once, then
// picks a different one when it ends - mirroring the console menu
// music behaviour (shuffle across menu1..menu4). Single-track case
// still works: if there's only one file, it just repeats.
//
// Supported extensions are whatever AVAudioPlayer can decode out of
// the box: mp3, m4a, aac, wav, aiff, caf. OGG needs a third-party
// decoder; transcode with ffmpeg first.

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#include "MCLE_iOS_Audio.h"

@interface MCLE_AudioShuffler : NSObject <AVAudioPlayerDelegate>
@property (strong, nonatomic) NSArray<NSString*>* tracks;
@property (strong, nonatomic) AVAudioPlayer* current;
@property (strong, nonatomic) NSString* currentPath;
- (void)playRandom;
@end

@implementation MCLE_AudioShuffler

- (void)playRandom {
    if (self.tracks.count == 0) return;
    // Pick a path that isn't the one we just played (when >1 option).
    NSString* next = nil;
    for (int tries = 0; tries < 8; ++tries) {
        NSUInteger idx = arc4random_uniform((uint32_t)self.tracks.count);
        NSString* pick = self.tracks[idx];
        if (self.tracks.count == 1 || ![pick isEqualToString:self.currentPath]) {
            next = pick;
            break;
        }
    }
    if (!next) next = self.tracks.firstObject;
    NSURL* url = [NSURL fileURLWithPath:next];
    NSError* err = nil;
    AVAudioPlayer* p = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&err];
    if (!p || err) {
        NSLog(@"[mcle_audio] init player failed for %@: %@", next, err);
        return;
    }
    p.delegate = self;
    p.numberOfLoops = 0;   // single play; delegate picks the next one
    p.volume = 1.0f;
    [p prepareToPlay];
    if (![p play]) {
        NSLog(@"[mcle_audio] play failed for %@", next);
        return;
    }
    self.current = p;
    self.currentPath = next;
    NSLog(@"[mcle_audio] playing %@ (%lu tracks available)",
          next.lastPathComponent, (unsigned long)self.tracks.count);
}

- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer*)player successfully:(BOOL)flag {
    // On success, pick another random track. On failure, don't loop
    // forever on a bad file - bail out.
    if (flag) {
        [self playRandom];
    } else {
        NSLog(@"[mcle_audio] track ended unsuccessfully, stopping shuffle");
    }
}

- (void)stop {
    [self.current stop];
    self.current = nil;
    self.currentPath = nil;
}

@end

namespace {
MCLE_AudioShuffler* g_shuffler = nil;

NSArray<NSString*>* findAllMenuTracks(void) {
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (!docs) return @[];
    NSFileManager* fm = [NSFileManager defaultManager];
    NSArray<NSString*>* extsAllowed = @[@"mp3", @"m4a", @"aac", @"wav", @"aiff", @"caf"];
    NSMutableArray<NSString*>* out = [NSMutableArray array];
    NSArray<NSString*>* entries = [fm contentsOfDirectoryAtPath:docs error:nil];
    for (NSString* e in entries) {
        if (![extsAllowed containsObject:e.pathExtension.lowercaseString]) continue;
        // Bias toward files whose names look musical: menu*, calm*,
        // menu_music*, minecraft*. If none of those exist we fall
        // through to all supported files.
        NSString* lower = e.lowercaseString;
        if ([lower hasPrefix:@"menu"] || [lower hasPrefix:@"calm"]
            || [lower hasPrefix:@"minecraft"] || [lower hasPrefix:@"hal"]
            || [lower hasPrefix:@"piano"]) {
            [out addObject:[docs stringByAppendingPathComponent:e]];
        }
    }
    if (out.count > 0) return out;
    // Fallback: any supported audio file.
    for (NSString* e in entries) {
        if ([extsAllowed containsObject:e.pathExtension.lowercaseString]) {
            [out addObject:[docs stringByAppendingPathComponent:e]];
        }
    }
    return out;
}
} // namespace

extern "C" int mcle_audio_start_menu_music(void) {
    if (g_shuffler && g_shuffler.current.playing) return 1;

    // Route audio to playback category so it doesn't duck for silent
    // switch / lock screen music.
    NSError* sessionErr = nil;
    [[AVAudioSession sharedInstance]
        setCategory:AVAudioSessionCategoryPlayback
              error:&sessionErr];
    if (sessionErr) {
        NSLog(@"[mcle_audio] AVAudioSession setCategory err=%@", sessionErr);
    }
    [[AVAudioSession sharedInstance] setActive:YES error:nil];

    NSArray<NSString*>* tracks = findAllMenuTracks();
    if (tracks.count == 0) {
        NSLog(@"[mcle_audio] no supported audio files found in Documents");
        return 0;
    }
    g_shuffler = [[MCLE_AudioShuffler alloc] init];
    g_shuffler.tracks = tracks;
    [g_shuffler playRandom];
    return g_shuffler.current ? 1 : -1;
}

extern "C" void mcle_audio_stop_menu_music(void) {
    if (g_shuffler) {
        [g_shuffler stop];
        g_shuffler = nil;
    }
    [[AVAudioSession sharedInstance] setActive:NO
                                   withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                         error:nil];
}
