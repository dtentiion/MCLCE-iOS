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
        @"Drop your own LCE SWF files in this folder.\n\n"
        @"Preferred filename: MainMenu1080.swf\n"
        @"If that is not present, the app will play the first .swf it\n"
        @"finds here. Without any SWF it falls back to a tiny built-in\n"
        @"test rectangle.\n\n"
        @"See INSTALL.md in the repo for how to extract SWFs from an LCE\n"
        @"install using scripts/list-arc.py or PCK Studio.\n";
    NSURL* readmeURL = [docs URLByAppendingPathComponent:@"README.txt"];
    [readme writeToURL:readmeURL
            atomically:YES
              encoding:NSUTF8StringEncoding
                 error:nil];
}

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {

    // Ensure Documents/ is non-empty so iOS exposes it under Files ->
    // On My iPhone. Without this the folder stays hidden until the app
    // writes something, which makes dropping user-supplied SWFs awkward.
    [self primeDocumentsFolder];

    // Start listening for controllers before the view controller comes up so
    // we don't miss a connect notification from an already-paired pad.
    mcle_ios_input_init();

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
    // mcle_swf_init tells us what happened.
    if (mcle_swf_init() != 0) {
        NSLog(@"[AppDelegate] mcle_swf_init reported failure; continuing");
    } else {
        // Load the bundled test SWF so the render_handler path is driven
        // by a real movie rather than our synthetic rect.
        NSString* swfPath = [[NSBundle mainBundle] pathForResource:@"test_rect"
                                                            ofType:@"swf"];
        if (swfPath.length) {
            NSLog(@"[AppDelegate] bundle test_rect.swf -> %@", swfPath);
            if (mcle_swf_load([swfPath UTF8String]) != 0) {
                NSLog(@"[AppDelegate] mcle_swf_load returned non-zero");
            }
        } else {
            NSLog(@"[AppDelegate] test_rect.swf not found in bundle");
            // Dump a short listing so we can tell from on-screen logs
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

- (void)applicationWillTerminate:(UIApplication*)application {
    mcle_swf_shutdown();
    mcle_ios_input_shutdown();
}

@end
