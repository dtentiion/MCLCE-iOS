#import "AppDelegate.h"
#import "MinecraftViewController.h"

#include "INP_iOS_Bridge.h"
#include "mcle_swf_bridge.h"
#include "ruffle_ios.h"

// Shared with MinecraftViewController so the overlay can display the
// probe result. Simple global is fine for this debug-only path.
int g_ruffle_swf_version = -99;
int g_ruffle_player_ok = -99;  // 1 = Player instantiated, 0 = failed
int g_ruffle_framerate_mHz = -99;
int g_ruffle_surface_probe = -99;  // 1 = wgpu Surface over CAMetalLayer OK

// The real wgpu-backed player created in MinecraftViewController::viewWillAppear.
// Owned by the view controller; nil if wgpu setup failed.
PlayerHandle* g_ruffle_player = NULL;

@implementation AppDelegate

- (void)primeDocumentsFolder {
    NSFileManager* fm = NSFileManager.defaultManager;
    NSURL* docs = [fm URLsForDirectory:NSDocumentDirectory
                             inDomains:NSUserDomainMask].firstObject;
    if (!docs) return;

    // Make sure the folder exists (it should, but be explicit).
    [fm createDirectoryAtURL:docs
 withIntermediateDirectories:YES
                  attributes:nil
                       error:nil];

    // Write a single README so the folder is visible in Files.app. Rewrite
    // every launch so updates to the text propagate; a few hundred bytes.
    NSString* readme =
        @"Minecraft LCE for iOS\n"
        @"=====================\n\n"
        @"Copy the Common, Windows64Media and music folders from your own\n"
        @"copy of the TU19 Windows64 build into this folder, then close the\n"
        @"app fully and open it again.\n\n"
        @"For PlayStation button icons, also copy OrbisMedia/Media from the\n"
        @"TU19 source.\n\n"
        @"See INSTALL.md in the repo for more.\n";
    NSURL* readmeURL = [docs URLByAppendingPathComponent:@"README.txt"];
    [readme writeToURL:readmeURL
            atomically:YES
              encoding:NSUTF8StringEncoding
                 error:nil];

    [self unpackWindows64Install:docs.path];
}

static uint32_t MCLEReadBigEndian32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// The Windows64 install copied in as it is. CMinecraftApp reads its media
// out of Common/Media/MediaWindows64.arc (ArchiveFile), the port reads the
// same files loose from the top of the folder, so they're unpacked there.
// Files already there are left alone.
- (void)unpackWindows64Install:(NSString*)docs {
    if (!docs.length) return;
    NSFileManager* fm = NSFileManager.defaultManager;
    NSString* arcPath = [docs stringByAppendingPathComponent:@"Common/Media/MediaWindows64.arc"];
    NSData* arc = [NSData dataWithContentsOfFile:arcPath options:NSDataReadingMappedIfSafe error:nil];
    if (arc.length >= 4) {
        const uint8_t* d = static_cast<const uint8_t*>(arc.bytes);
        const size_t size = arc.length;
        const uint32_t count = MCLEReadBigEndian32(d);
        size_t pos = 4;
        int written = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (pos + 2 > size) break;
            const size_t nameLen = ((size_t)d[pos] << 8) | d[pos + 1];
            pos += 2;
            if (pos + nameLen + 8 > size) break;
            NSString* name = [[NSString alloc] initWithBytes:d + pos length:nameLen encoding:NSUTF8StringEncoding];
            pos += nameLen;
            const uint32_t offset = MCLEReadBigEndian32(d + pos);
            const uint32_t length = MCLEReadBigEndian32(d + pos + 4);
            pos += 8;
            // A leading * is a compressed entry to ArchiveFile, Windows64's has none
            if (!name.length || [name hasPrefix:@"*"] || (uint64_t)offset + length > size) continue;
            name = [name stringByReplacingOccurrencesOfString:@"\\" withString:@"/"];
            if ([name hasPrefix:@"/"] || [name containsString:@".."]) continue;
            NSString* out = [docs stringByAppendingPathComponent:name];
            if ([fm fileExistsAtPath:out]) continue;
            [fm createDirectoryAtPath:out.stringByDeletingLastPathComponent
          withIntermediateDirectories:YES attributes:nil error:nil];
            if ([[arc subdataWithRange:NSMakeRange(offset, length)] writeToFile:out atomically:YES]) ++written;
        }
        if (written) NSLog(@"[AppDelegate] unpacked %d files from MediaWindows64.arc", written);
    }

    // The loose files read from the top of the folder
    NSArray<NSArray<NSString*>*>* loose = @[
        @[ @"Common/Media/font/Mojang Font_7.ttf", @"Mojang Font_7.ttf" ],
        @[ @"Common/Media/font/Mojang Font_11.ttf", @"Mojang Font_11.ttf" ],
        // The PS4 skins for a PlayStation pad, from the source's OrbisMedia
        @[ @"OrbisMedia/Media/skinHDOrbis.swf", @"skinHDOrbis.swf" ],
        @[ @"OrbisMedia/Media/skinOrbis.swf", @"skinOrbis.swf" ],
    ];
    for (NSArray<NSString*>* file in loose) {
        NSString* from = [docs stringByAppendingPathComponent:file[0]];
        NSString* to = [docs stringByAppendingPathComponent:file[1]];
        if ([fm fileExistsAtPath:from] && ![fm fileExistsAtPath:to]) [fm copyItemAtPath:from toPath:to error:nil];
    }
}

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {

    // Ensure Documents/ is non-empty so iOS exposes it under Files ->
    // On My iPhone. Without this the folder stays hidden until the app
    // writes something, which makes dropping user-supplied SWFs awkward.
    [self primeDocumentsFolder];

    // Start listening for controllers before the view controller comes up so
    // a connect notification from an already-paired pad isn't missed.
    mcle_ios_input_init();

    // Controller input doesn't reset the idle timer and there's no touch
    // input, so without this the screen auto-locks mid game and the app
    // gets suspended.
    application.idleTimerDisabled = YES;

    // Confirm the Rust Ruffle crate actually linked in and runs natively.
    int rust_magic = ruffle_ios_magic();
    NSLog(@"[AppDelegate] ruffle_ios_magic = 0x%08X (expected 0x52554646 'RUFF')",
          rust_magic);
    ruffle_ios_init();

    // Probe-parse the bundled test SWF through Ruffle's parser. Result is
    // stashed globally and shown on the status overlay.
    NSString* probePath = [[NSBundle mainBundle] pathForResource:@"test_rect"
                                                          ofType:@"swf"];
    extern int g_ruffle_swf_version;
    g_ruffle_swf_version = -99;
    if (probePath.length) {
        NSData* data = [NSData dataWithContentsOfFile:probePath];
        if (data.length) {
            g_ruffle_swf_version =
                ruffle_ios_swf_probe((const uint8_t*)data.bytes, data.length);
            NSLog(@"[AppDelegate] ruffle_ios_swf_probe -> %d", g_ruffle_swf_version);

            // Real Ruffle Player: build one with the SWF pre-loaded, tick it
            // a few times to make sure nothing panics, then tear it down.
            PlayerHandle* h = ruffle_ios_player_create_with_swf(
                640, 480, (const uint8_t*)data.bytes, data.length);
            if (h) {
                g_ruffle_player_ok = 1;
                g_ruffle_framerate_mHz = ruffle_ios_player_framerate_mHz(h);
                for (int i = 0; i < 3; ++i) {
                    ruffle_ios_player_tick(h, 1.0f / 30.0f);
                }
                ruffle_ios_player_destroy(h);
                NSLog(@"[AppDelegate] ruffle player ok, fps=%.3f",
                      g_ruffle_framerate_mHz / 1000.0);
            } else {
                g_ruffle_player_ok = 0;
                NSLog(@"[AppDelegate] ruffle_ios_player_create_with_swf FAILED");
            }
        }
    }

    // Bring up the SWF runtime (render_handler + player). If this fails the
    // app still runs, just without future SWF-driven UI. The log line in
    // mcle_swf_init says what happened.
    if (mcle_swf_init() != 0) {
        NSLog(@"[AppDelegate] mcle_swf_init reported failure; continuing");
    } else {
        // Load the bundled test SWF so the render_handler path is driven
        // by a real movie rather than the synthetic rect.
        NSString* swfPath = [[NSBundle mainBundle] pathForResource:@"test_rect"
                                                            ofType:@"swf"];
        if (swfPath.length) {
            NSLog(@"[AppDelegate] bundle test_rect.swf -> %@", swfPath);
            if (mcle_swf_load([swfPath UTF8String]) != 0) {
                NSLog(@"[AppDelegate] mcle_swf_load returned non-zero");
            }
        } else {
            NSLog(@"[AppDelegate] test_rect.swf not found in bundle");
            // Dump a short listing so on-screen logs show
            // roughly what IS in the bundle.
            NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
            NSArray* entries = [[NSFileManager defaultManager]
                contentsOfDirectoryAtPath:bundlePath error:nil];
            NSLog(@"[AppDelegate] bundle contents: %@", entries);
        }
    }

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [[MinecraftViewController alloc] init];
    [self.window makeKeyAndVisible];

    return YES;
}

// Persist chunk data on iOS lifecycle events, defined in
// Game/MCLEGameLoop.cpp
extern "C" void mcle_save_now_sync(void);

- (void)applicationDidEnterBackground:(UIApplication*)application {
    // The main thread MUST return from this callback quickly. Earlier
    // attempts called mcle_save_now_sync() inline here and the iOS
    // watchdog killed the app mid-save (log: BG_SAVE starts, sim keeps
    // ticking, then process dies before any per-level "done" line).
    //
    // The correct shape is:
    //   1. beginBackgroundTask to tell iOS there is real work pending -
    //      the kernel extends the background runtime to ~30 seconds.
    //   2. Dispatch the blocking save onto a global queue so the main
    //      thread returns immediately, keeping the watchdog happy.
    //   3. endBackgroundTask only after the save thread actually
    //      finishes, so iOS doesn't suspend the app mid-write.
    __block UIBackgroundTaskIdentifier task = UIBackgroundTaskInvalid;
    task = [application beginBackgroundTaskWithName:@"mcle.save"
                                  expirationHandler:^{
        if (task != UIBackgroundTaskInvalid) {
            [application endBackgroundTask:task];
            task = UIBackgroundTaskInvalid;
        }
    }];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        mcle_save_now_sync();
        if (task != UIBackgroundTaskInvalid) {
            [application endBackgroundTask:task];
            task = UIBackgroundTaskInvalid;
        }
    });
}

- (void)applicationWillTerminate:(UIApplication*)application {
    // Terminate path: iOS gives only a few seconds and there's no
    // beginBackgroundTask escape hatch - run the save inline and hope
    // it finishes before the kernel pulls the plug.
    mcle_save_now_sync();
    mcle_swf_shutdown();
    mcle_ios_input_shutdown();
}

@end
