#import "MinecraftViewController.h"
#import "MetalView.h"

#import <QuartzCore/CADisplayLink.h>

#include "RND_iOS_Stub.h"
#include "INP_iOS_Bridge.h"
#include "mcle_swf_bridge.h"
#include "ruffle_ios.h"
#include "MCLE_iOS_Audio.h"

extern "C" void mcle_game_tick(void);  // GameBootstrap.cpp
extern "C" unsigned long long mcle_swf_total_mesh_strips(void);
extern "C" unsigned long long mcle_swf_total_triangles(void);
extern "C" unsigned long long mcle_swf_total_frames(void);
extern "C" unsigned long long mcle_swf_total_bitmap_draws(void);
extern "C" unsigned long long mcle_swf_total_line_strips(void);
extern "C" unsigned long long mcle_swf_total_masks(void);
extern "C" unsigned long long mcle_swf_total_fill_bitmaps(void);

@interface MinecraftViewController ()
@property (strong, nonatomic) CADisplayLink* displayLink;
@property (strong, nonatomic) UILabel* statusLabel;
@property (strong, nonatomic) MetalView* metalView;
@property (strong, nonatomic) NSString* loadedSwfName;  // "MainMenu1080.swf" or "test_rect.swf (bundled)"
@property (strong, nonatomic) NSString* fontStatus;     // "Mojangles7 ok (69k)" or "Mojangles7 missing"
@property (nonatomic) uint32_t lastPadButtons;          // last seen 4J button bitmask, for edge detection
@property (nonatomic) int menuFocusIndex;               // index into menuButtonConfig
@property (strong, nonatomic) NSString* currentMenuSwf; // "MainMenu1080.swf", "HelpAndOptionsMenu1080.swf", etc.
// Array of NSDictionary entries with keys "name" (e.g. "Button1"),
// "label" (display text), "id" (NSNumber int matching the console's
// eControl_* enum). Configured per-menu by initXxxButtons methods so
// the focus state machine can drive any menu uniformly.
@property (strong, nonatomic) NSArray<NSDictionary*>* menuButtonConfig;
// Sibling scenery (panorama / logo / tooltips) is attached to the
// Stage once and persists across replace_root_movie calls, matching
// how console UIComponent_Panorama lives on the parent layer rather
// than the scene. This flag guards against re-attach on every menu
// transition.
@property (nonatomic) BOOL scenerAttached;
@end

@implementation MinecraftViewController

// Find a SWF to play. Preference order:
//   1. Documents/MainMenu1080.swf  (user-supplied via Files app)
//   2. Documents/*.swf             (anything the user dropped in)
//   3. Bundled test_rect.swf       (built-in fallback)
- (NSString*)resolveSwfPath {
    NSFileManager* fm = NSFileManager.defaultManager;
    NSURL* docs = [fm URLsForDirectory:NSDocumentDirectory
                             inDomains:NSUserDomainMask].firstObject;
    NSString* docsPath = docs.path;

    // Preferred explicit filename: MainMenu1080.swf.
    if (docsPath.length) {
        NSString* preferred = [docsPath stringByAppendingPathComponent:@"MainMenu1080.swf"];
        if ([fm fileExistsAtPath:preferred]) return preferred;

        // Next, scan the Documents root for any .swf.
        NSArray<NSString*>* entries = [fm contentsOfDirectoryAtPath:docsPath error:nil];
        for (NSString* e in entries) {
            if ([e.pathExtension caseInsensitiveCompare:@"swf"] == NSOrderedSame) {
                return [docsPath stringByAppendingPathComponent:e];
            }
        }
    }

    // Fallback to the bundled test movie so we always render something.
    return [[NSBundle mainBundle] pathForResource:@"test_rect" ofType:@"swf"];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;

    CGRect frame = self.view.bounds;

    // Metal-backed view fills the whole screen. Renderer draws into its layer.
    self.metalView = [[MetalView alloc] initWithFrame:frame];
    self.metalView.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.metalView.opaque = YES;
    self.metalView.backgroundColor = UIColor.blackColor;
    [self.view addSubview:self.metalView];

    // Overlay label with build info + live controller state. Sits inside the
    // safe area so the Dynamic Island does not chew it up on newer iPhones.
    CGRect safeFrame = CGRectInset(frame, 24, 24);
    self.statusLabel = [[UILabel alloc] initWithFrame:safeFrame];
    self.statusLabel.numberOfLines = 0;
    self.statusLabel.textColor = [UIColor colorWithRed:0.95 green:0.25 blue:0.35 alpha:1.0];
    self.statusLabel.backgroundColor = [UIColor clearColor];
    self.statusLabel.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
    self.statusLabel.text =
        @"Minecraft: Legacy Console Edition (iOS)\n\n"
        @"Early scaffold build. Renderer: Metal (triangle test).\n"
        @"Connect a controller to see input events.\n";
    self.statusLabel.textAlignment = NSTextAlignmentLeft;
    self.statusLabel.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.statusLabel];
}

- (void)viewSafeAreaInsetsDidChange {
    [super viewSafeAreaInsetsDidChange];
    // Keep the overlay label inside the safe area so notches do not clip it.
    CGRect b = self.view.bounds;
    UIEdgeInsets s = self.view.safeAreaInsets;
    self.statusLabel.frame = CGRectMake(
        s.left + 16, s.top + 12,
        b.size.width  - s.left - s.right  - 32,
        b.size.height - s.top  - s.bottom - 24);
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];

    CGSize sz = self.metalView.bounds.size;
    CGFloat scale = self.view.window.screen.nativeScale ?: UIScreen.mainScreen.nativeScale;
    int pw = (int)(sz.width  * scale);
    int ph = (int)(sz.height * scale);
    self.metalView.layer.contentsScale = scale;

    // Try to stand up a Ruffle wgpu player that owns this CAMetalLayer.
    // If it succeeds, every frame is driven by Ruffle. If it fails, we
    // fall back to our legacy Metal triangle path for diagnostics.
    extern PlayerHandle* g_ruffle_player;
    extern int g_ruffle_surface_probe;

    g_ruffle_surface_probe =
        ruffle_ios_surface_probe((__bridge void*)self.metalView.layer);

    NSString* swfPath = [self resolveSwfPath];
    if (swfPath.length && g_ruffle_surface_probe == 1) {
        NSData* data = [NSData dataWithContentsOfFile:swfPath];
        if (data.length) {
            NSString* docsPath = [NSSearchPathForDirectoriesInDomains(
                NSDocumentDirectory, NSUserDomainMask, YES) firstObject];

            // Stage the LCE menu font BEFORE create_wgpu so Ruffle registers
            // it before preload. Registering after returns caches misses in
            // text-field glyph tables and labels render as empty boxes.
            // LCE ships the file as "Mojang Font_7.ttf"; also try the name
            // the SWF references ("Mojangles7.ttf") and a few common
            // subfolders for users who organise their Files app drop.
            NSArray<NSString*>* fontNames = @[
                @"Mojangles7.ttf",
                @"Mojang Font_7.ttf",
            ];
            NSArray<NSString*>* fontSubdirs = @[
                @"",
                @"fonts",
                @"font",
                @"Fonts",
                @"Font",
            ];
            NSFileManager* fm = [NSFileManager defaultManager];
            NSString* foundAt = nil;
            NSData* fontData = nil;
            for (NSString* subdir in fontSubdirs) {
                NSString* dir = subdir.length
                    ? [docsPath stringByAppendingPathComponent:subdir]
                    : docsPath;
                for (NSString* name in fontNames) {
                    NSString* p = [dir stringByAppendingPathComponent:name];
                    if ([fm fileExistsAtPath:p]) {
                        fontData = [NSData dataWithContentsOfFile:p];
                        if (fontData.length) { foundAt = p; break; }
                    }
                }
                if (fontData.length) break;
            }
            if (fontData.length) {
                const char* nameCStr = "Mojangles7";
                int rc = ruffle_ios_stage_device_font(
                    (const uint8_t*)nameCStr, strlen(nameCStr),
                    (const uint8_t*)fontData.bytes, fontData.length,
                    0, 0);
                NSLog(@"[MinecraftVC] staged device font from %@ rc=%d",
                      foundAt, rc);
                if (rc == 1) {
                    self.fontStatus = [NSString stringWithFormat:
                        @"Mojangles7 staged (%luk) from %@",
                        (unsigned long)(fontData.length / 1024),
                        [foundAt stringByReplacingOccurrencesOfString:docsPath
                                                           withString:@"Docs"]];
                } else {
                    self.fontStatus = [NSString stringWithFormat:
                        @"Mojangles7 stage rc=%d", rc];
                }
            } else {
                // List what's actually in Documents so the user can see
                // whether the file even landed where we expect.
                NSError* err = nil;
                NSArray<NSString*>* entries =
                    [fm contentsOfDirectoryAtPath:docsPath error:&err];
                NSString* summary = err
                    ? [NSString stringWithFormat:@"err=%@", err.localizedDescription]
                    : [[entries subarrayWithRange:NSMakeRange(0, MIN((NSUInteger)6, entries.count))]
                        componentsJoinedByString:@","];
                self.fontStatus = [NSString stringWithFormat:
                    @"Mojangles7 missing; Docs has: %@", summary];
            }

            // Now that fonts are staged, build the player. create_wgpu will
            // apply the staged fonts between PlayerBuilder::build() and the
            // preload loop, so text fields find the device font on their
            // first glyph layout.
            g_ruffle_player = ruffle_ios_player_create_wgpu(
                (__bridge void*)self.metalView.layer,
                pw, ph,
                (const uint8_t*)data.bytes, data.length,
                docsPath.UTF8String);
            self.currentMenuSwf = swfPath.lastPathComponent;
            BOOL fromDocs = [swfPath rangeOfString:@"/Documents/"].location != NSNotFound;
            self.loadedSwfName = [NSString stringWithFormat:@"%@ (%@)",
                swfPath.lastPathComponent,
                fromDocs ? @"Documents" : @"bundled"];
            NSLog(@"[MinecraftVC] ruffle wgpu player = %p  (loaded %@)",
                  g_ruffle_player, self.loadedSwfName);

            // One-shot discovery dump. LCE SWFs are driven by the host calling
            // AS3 methods on named button instances (IggyPlayerCallMethodRS
            // on console). We don't know the instance names ahead of time, so
            // enumerate the root's direct children and write them into the
            // persistent log where we can read them off-device.
            if (g_ruffle_player) {
                static uint8_t childBuf[8192];
                size_t n = ruffle_ios_enumerate_root_children(
                    g_ruffle_player, childBuf, sizeof(childBuf));
                NSString* childList = n
                    ? [[NSString alloc] initWithBytes:childBuf length:n
                                             encoding:NSUTF8StringEncoding]
                    : @"<empty>";
                NSLog(@"[MinecraftVC] root children:\n%@", childList);

                // Populate the main menu buttons + attach panorama via
                // the shared -initMainMenuButtons path (also used on
                // transition back from submenus).
                [self initMainMenuButtons];
            }
        }
    }
    if (!self.loadedSwfName) self.loadedSwfName = @"<none>";

    if (!g_ruffle_player) {
        // Fallback: our hand-rolled Metal pipeline drew the triangle before.
        mcle_render_init((__bridge void*)self.metalView.layer, pw, ph);
    }

    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];

    // Start menu music if the user has a supported audio file in
    // Documents. Silent no-op otherwise.
    int mrc = mcle_audio_start_menu_music();
    NSLog(@"[MinecraftVC] menu music start rc=%d", mrc);
}

- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    mcle_audio_stop_menu_music();
    [self.displayLink invalidate];
    self.displayLink = nil;
    mcle_render_shutdown();
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGSize sz = self.metalView.bounds.size;
    CGFloat scale = self.view.window.screen.nativeScale ?: UIScreen.mainScreen.nativeScale;
    mcle_render_resize((int)(sz.width * scale), (int)(sz.height * scale));
}

- (void)applyMenuButtonConfig:(NSArray<NSDictionary*>*)config {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    self.menuButtonConfig = config;
    for (NSDictionary* entry in config) {
        const char* name = [entry[@"name"] UTF8String];
        const char* label = [entry[@"label"] UTF8String];
        double id = [entry[@"id"] doubleValue];
        ruffle_ios_call_init_on_named_child(
            g_ruffle_player,
            (const uint8_t*)name,  strlen(name),
            (const uint8_t*)"Init", 4,
            (const uint8_t*)label, strlen(label),
            id);
        ruffle_ios_call_init_on_named_child(
            g_ruffle_player,
            (const uint8_t*)name,      strlen(name),
            (const uint8_t*)"SetLabel", 8,
            (const uint8_t*)label,     strlen(label),
            0.0);
    }
    if (config.count > 0) {
        const char* firstName = [config[0][@"name"] UTF8String];
        ruffle_ios_call_init_on_named_child(
            g_ruffle_player,
            (const uint8_t*)firstName, strlen(firstName),
            (const uint8_t*)"ChangeState", 11,
            (const uint8_t*)"", 0,
            0.0);
    }
    self.menuFocusIndex = 0;

    // Hide 4J's loading placeholder on every scene. Iggy on console
    // hides it automatically when the scene finishes streaming; our
    // port has no scene-ready hook wired up, so we hide it explicitly
    // once buttons are wired.
    const char* splashName = "iggy_Splash";
    ruffle_ios_set_root_child_visible(
        g_ruffle_player,
        (const uint8_t*)splashName, strlen(splashName),
        0);
}

- (void)attachMenuScenery {
    // Mirrors UIScene_MainMenu.cpp:29-30 + UIGroup.cpp:28 on console
    // which composite Panorama + Logo + Tooltips as sibling movies
    // attached to the parent UILayer (not the scene). The layer
    // outlives scene transitions, so the panorama timeline keeps
    // playing smoothly as menus swap.
    //
    // Our Ruffle side now attaches these to the Stage at non-zero
    // depths (Stage.replace_root_movie only touches depth 0, so
    // Stage-level children at other depths survive scene swaps).
    // That means we should attach ONCE at startup, NOT on every
    // scene init - otherwise each transition would create duplicate
    // siblings and reset the panorama's playhead to frame 1.
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    if (self.scenerAttached) return;
    self.scenerAttached = YES;
    {
        // Console Iggy layers each scene as its own movie. Panorama
        // is Panorama1080.swf, a sibling of MainMenu1080.swf that the
        // host composites beneath it. Before loading the SWF, register
        // any matching PNGs in Documents as Ruffle Bitmap characters
        // keyed by class name - this mirrors 4J's Iggy XUI texture-
        // import mechanism where skin_Minecraft.xui maps AS3 class
        // names (Panorama_Background_S etc.) to external PNG paths.
        NSString* docs = [NSSearchPathForDirectoriesInDomains(
            NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
        // XUI import scale matches skin_Minecraft.xui <Scale> elements.
        // Panorama_Background_S/N carry Scale=5.0 there so the 820x144
        // tile renders as 4100x720; without that, two tiles side-by-side
        // don't cover the stage and a large gap appears between end and
        // start. MenuTitle has no XUI scale entry, it's drawn at native.
        NSArray* xuiAssets = @[
            @[@"Panorama_Background_S", @(5.0f), @(5.0f)],
            @[@"Panorama_Background_N", @(5.0f), @(5.0f)],
            @[@"MenuTitle",             @(1.0f), @(1.0f)],
        ];
        for (NSArray* entry in xuiAssets) {
            NSString* className = entry[0];
            float sx = [entry[1] floatValue];
            float sy = [entry[2] floatValue];
            NSString* pngPath = [docs stringByAppendingPathComponent:
                                 [className stringByAppendingString:@".png"]];
            NSData* pngData = [NSData dataWithContentsOfFile:pngPath];
            if (!pngData.length) {
                NSLog(@"[MinecraftVC] XUI %@ missing at %@", className, pngPath);
                continue;
            }
            int rc = ruffle_ios_register_xui_bitmap(
                g_ruffle_player,
                (const uint8_t*)className.UTF8String, strlen(className.UTF8String),
                (const uint8_t*)pngData.bytes, pngData.length,
                sx, sy);
            NSLog(@"[MinecraftVC] register_xui_bitmap %@ scale=%.1fx%.1f -> %d",
                  className, sx, sy, rc);
        }

        // Load the same sibling movies UIScene_MainMenu composites on
        // console (UIScene_MainMenu.cpp:29-30 adds eUIComponent_Panorama
        // + eUIComponent_Logo; UIGroup.cpp:28 adds Tooltips via the
        // tooltips layer). Depths: panorama beneath everything (0),
        // logo and tooltips above MainMenu's button depths (3..9) so
        // they overlay. Tooltips specifically covers the bottom area
        // the panorama doesn't reach (panorama is authored at 5x
        // scale => 720/1080 vertical coverage; the gap is the
        // tooltips overlay slot on console).
        // Each sibling's (depth, scaleX, scaleY). Console renders
        // each Iggy movie at screen resolution via
        // IggyPlayerSetDisplaySize so the authored 1920x1080
        // content fills the screen. Our shared stage means siblings
        // render at their authored proportions within the 1920x1080
        // stage, so content authored at less than the stage size
        // (panorama's 720-tall bitmap, logo's 571x138 PNG) looks
        // small on iPhone. Apply per-sibling scale to fill out the
        // visible area, since iPhone-friendly presentation is Path B
        // of the port direction (faithful to source for architecture,
        // adapted for device for visuals).
        //
        // Panorama: sy=1.5 so the 720-tall strip scales to cover the
        // full 1080 stage height. sx stays 1.0 so horizontal tile
        // scrolling geometry keeps working.
        //
        // Logo: 1.5x uniform so the 571x138 MenuTitle PNG is more
        // visible on a phone screen.
        //
        // Tooltips: keep 1.0x until we fix the individual button
        // panel rendering (FJ_Tooltips undefined-class issue).
        // Panorama: uniform 1.5x (same sx/sy) so authored aspect is
        // preserved. sx=1, sy=1.5 stretched mountains vertically and
        // the scene looked squished horizontally; matching sx to sy
        // fixes the aspect. ty stays 0 so the bottom still touches
        // the stage floor the way it did at sx=1,sy=1.5.
        //
        // Logo: 1.0x with the 1080.png MenuTitle (857x207) dropped into
        // Documents. The SWF is authored to center that asset on the
        // 1920x1080 stage, so no scale and no translate is needed.
        // Using the 720.png (571x138) asset instead looks small AND
        // scaling it 1.5x shifts the content because scale-from-origin
        // multiplies every authored x by sx. Stick with the native
        // size and let the SWF do its own layout.
        //
        // Tooltips: keep 1.0x until FJ_Tooltips is fleshed out.
        // Panorama tx=-208.6: cancels the stage viewport's letterbox
        // offset. The iPhone screen is wider than the 1920x1080 stage's
        // 16:9 aspect, so Ruffle's stage->screen transform adds ~226
        // screen px of left tx to fit-height-center. Metal doesn't
        // clip to the stage rect, so pre-shifting the panorama sibling
        // by (-226 / stage_scale) = -208.6 authored stage px lands
        // tile1 exactly at screen x=0 on frame 0, removing the brief
        // left-edge strip that showed for the first ~6 seconds while
        // tile1 scrolled into the letterbox area.
        struct SiblingCfg { NSString* swf; int depth; float sx; float sy; float tx; float ty; };
        NSArray* siblings = @[
            @[@"Panorama1080.swf",     @(-1),  @(1.5f), @(1.5f), @(-208.6f), @(0.0f)],
            @[@"ToolTips1080.swf",     @(100), @(1.0f), @(1.0f), @(0.0f),    @(0.0f)],
            @[@"ComponentLogo1080.swf",@(101), @(1.0f), @(1.0f), @(0.0f),    @(0.0f)],
        ];
        for (NSArray* entry in siblings) {
            NSString* swfName = entry[0];
            int depth = [entry[1] intValue];
            float sx = [entry[2] floatValue];
            float sy = [entry[3] floatValue];
            float tx = [entry[4] floatValue];
            float ty = [entry[5] floatValue];
            NSString* path = [docs stringByAppendingPathComponent:swfName];
            NSData* data = [NSData dataWithContentsOfFile:path];
            if (!data.length) {
                NSLog(@"[MinecraftVC] %@ missing at %@", swfName, path);
                continue;
            }
            NSString* url = [NSString stringWithFormat:@"file://%@",
                [path stringByReplacingOccurrencesOfString:@" " withString:@"%20"]];
            int rc = ruffle_ios_add_sibling_swf_to_root(
                g_ruffle_player,
                (const uint8_t*)data.bytes, data.length,
                (const uint8_t*)url.UTF8String, strlen(url.UTF8String),
                depth, sx, sy, tx, ty);
            NSLog(@"[MinecraftVC] %@ (depth %d, scale %.1fx%.1f, t=%.0f,%.0f) -> %d",
                  swfName, depth, sx, sy, tx, ty, rc);
        }
    }
}

- (void)initMainMenuButtons {
    NSArray<NSDictionary*>* cfg = @[
        @{ @"name": @"Button1", @"label": @"Play Game",            @"id": @(0) },
        @{ @"name": @"Button2", @"label": @"Leaderboards",         @"id": @(1) },
        @{ @"name": @"Button3", @"label": @"Achievements",         @"id": @(2) },
        @{ @"name": @"Button4", @"label": @"Help & Options",       @"id": @(3) },
        @{ @"name": @"Button5", @"label": @"Downloadable Content", @"id": @(4) },
        @{ @"name": @"Button6", @"label": @"Exit Game",            @"id": @(5) },
    ];
    [self attachMenuScenery];
    [self applyMenuButtonConfig:cfg];
}

- (void)initHelpAndOptionsButtons {
    // Mirrors UIScene_HelpAndOptionsMenu constructor on console: only the
    // five universally-visible buttons are configured (Reinstall and
    // Debug are runtime-removed in the console release build). BUTTON_HAO_*
    // ids match the enum on console.
    NSArray<NSDictionary*>* cfg = @[
        @{ @"name": @"Button1", @"label": @"Change Skin",  @"id": @(0) },
        @{ @"name": @"Button2", @"label": @"How to Play",  @"id": @(1) },
        @{ @"name": @"Button3", @"label": @"Controls",     @"id": @(2) },
        @{ @"name": @"Button4", @"label": @"Settings",     @"id": @(3) },
        @{ @"name": @"Button5", @"label": @"Credits",      @"id": @(4) },
    ];
    [self attachMenuScenery];
    [self applyMenuButtonConfig:cfg];
    // Hide the two unused slots (Button6=Reinstall Content,
    // Button7=Debug Settings). Console removeControl()s them in
    // UIScene_HelpAndOptionsMenu's ctor; we call FJ_Button::HideUntilInit
    // which sets .visible=false and marks them non-initialised, same
    // practical effect of keeping them off the display.
    extern PlayerHandle* g_ruffle_player;
    for (NSString* hiddenName in @[@"Button6", @"Button7"]) {
        const char* n = hiddenName.UTF8String;
        ruffle_ios_call_init_on_named_child(
            g_ruffle_player,
            (const uint8_t*)n, strlen(n),
            (const uint8_t*)"HideUntilInit", 13,
            (const uint8_t*)"", 0,
            0.0);
    }
}

- (void)transitionToMenuNamed:(NSString*)swfName {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    NSString* docsPath = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    NSString* path = [docsPath stringByAppendingPathComponent:swfName];
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!data.length) {
        NSLog(@"[MinecraftVC] transition: %@ not found at %@", swfName, path);
        return;
    }
    NSString* url = [NSString stringWithFormat:@"file://%@",
                     [path stringByReplacingOccurrencesOfString:@" " withString:@"%20"]];
    int rc = ruffle_ios_player_replace_swf(
        g_ruffle_player,
        (const uint8_t*)data.bytes, data.length,
        (const uint8_t*)url.UTF8String, strlen(url.UTF8String));
    NSLog(@"[MinecraftVC] transition -> %@ rc=%d", swfName, rc);
    if (rc == 1) {
        self.currentMenuSwf = swfName;
        self.menuFocusIndex = 0;
        // Advance the scene HEADLESS (no render) so its async imports
        // finish and Button1..ButtonN get placed, but the surface still
        // shows the previous frame. This matches console's scene-swap
        // semantics: initialiseMovie + control init happen before the
        // scene becomes visible. Without this, the new menu renders
        // with Flash authoring-time placeholder text ("FJ_Label") for
        // the ~500ms it takes our Init calls to land.
        for (int i = 0; i < 30; ++i) {
            // Use the preserve-xui variant so the panorama, logo, and
            // tooltips don't advance during the 30-tick burst that lets
            // the new scene's init chain settle. Without this the
            // panorama visibly jumped ~22 authored px leftward each
            // time the user entered or exited a sub-menu.
            ruffle_ios_player_tick_headless_preserve_xui(
                g_ruffle_player, 1.0f / 60.0f);
        }
        if ([swfName isEqualToString:@"MainMenu1080.swf"]) {
            [self initMainMenuButtons];
        } else if ([swfName isEqualToString:@"HelpAndOptionsMenu1080.swf"]) {
            [self initHelpAndOptionsButtons];
        }
        // Dump the new menu's root children so we know what buttons/
        // named instances to drive next. Labels aren't Init'd yet for
        // non-MainMenu scenes; that's a per-scene string table follow-up.
        static uint8_t childBuf[4096];
        size_t n = ruffle_ios_enumerate_root_children(
            g_ruffle_player, childBuf, sizeof(childBuf));
        NSString* childList = n
            ? [[NSString alloc] initWithBytes:childBuf length:n
                                     encoding:NSUTF8StringEncoding]
            : @"<empty>";
        NSLog(@"[MinecraftVC] %@ root children:\n%@", swfName, childList);
    }
}

- (void)tick:(CADisplayLink*)link {
    mcle_game_tick();

    extern PlayerHandle* g_ruffle_player;

    // Edge-detect controller buttons and forward press/release events to
    // Ruffle so the menu's AS3 input handlers fire. Bit layout matches
    // 4J_Input.h; the int code we pass matches ruffle_ios.h's mapping.
    if (g_ruffle_player) {
        mcle_ios_pad_state pad;
        if (mcle_ios_input_poll(0, &pad)) {
            static const struct { uint32_t mask; int code; } btnMap[] = {
                { 0x00000001u,  0 },  // A     -> South
                { 0x00000002u,  1 },  // B     -> East
                { 0x00000008u,  2 },  // Y     -> North
                { 0x00000004u,  3 },  // X     -> West
                { 0x00000010u,  4 },  // START
                { 0x00000020u,  5 },  // BACK  -> Select
                { 0x00000400u,  6 },  // DPadUp
                { 0x00000800u,  7 },  // DPadDown
                { 0x00001000u,  8 },  // DPadLeft
                { 0x00002000u,  9 },  // DPadRight
                { 0x00000080u, 10 },  // LB    -> LeftTrigger
                { 0x00000040u, 11 },  // RB    -> RightTrigger
                { 0x00800000u, 12 },  // LT    -> LeftTrigger2
                { 0x00400000u, 13 },  // RT    -> RightTrigger2
            };
            uint32_t now = pad.buttons;
            uint32_t prev = self.lastPadButtons;
            uint32_t changed = now ^ prev;
            for (size_t i = 0; i < sizeof(btnMap)/sizeof(btnMap[0]); ++i) {
                if (changed & btnMap[i].mask) {
                    if (now & btnMap[i].mask) {
                        ruffle_ios_player_gamepad_down(g_ruffle_player, btnMap[i].code);
                    } else {
                        ruffle_ios_player_gamepad_up(g_ruffle_player, btnMap[i].code);
                    }
                }
            }
            self.lastPadButtons = now;

            // Menu focus state machine, driven from the per-scene
            // menuButtonConfig. Each entry has a "name"/"label"/"id".
            // FJ_Button.ChangeState codes: 1=SELECTED, 2=UNSELECTED,
            // 3=PRESSED. We edge-detect DPadUp/Down to walk the list and
            // A to fire the press animation + any scene action.
            NSArray<NSDictionary*>* cfg = self.menuButtonConfig;
            int count = (int)cfg.count;
            uint32_t pressedNow = changed & now;  // buttons that went 0 -> 1
            auto changeState = ^(int idx, int state) {
                if (idx < 0 || idx >= count) return;
                const char* name = [cfg[idx][@"name"] UTF8String];
                ruffle_ios_call_init_on_named_child(
                    g_ruffle_player,
                    (const uint8_t*)name, strlen(name),
                    (const uint8_t*)"ChangeState", 11,
                    (const uint8_t*)"", 0,
                    (double)state);
            };
            if (count > 0) {
                int cur = self.menuFocusIndex;
                int next = cur;
                if (pressedNow & 0x00000400u) next = (cur + count - 1) % count;  // DPadUp
                if (pressedNow & 0x00000800u) next = (cur + 1) % count;          // DPadDown
                if (next != cur) {
                    changeState(cur, 2);      // UNSELECTED
                    changeState(next, 1);     // SELECTED
                    self.menuFocusIndex = next;
                    NSLog(@"[MinecraftVC] menu focus -> %@",
                          cfg[next][@"name"]);
                }
                if (pressedNow & 0x00000001u) {  // A -> PRESSED
                    changeState(cur, 3);
                    int id = [cfg[cur][@"id"] intValue];
                    NSLog(@"[MinecraftVC] menu press -> %@ (id=%d)",
                          cfg[cur][@"name"], id);
                    // Scene transitions per current menu + pressed id.
                    if ([self.currentMenuSwf isEqualToString:@"MainMenu1080.swf"]) {
                        if (id == 3) {
                            [self transitionToMenuNamed:@"HelpAndOptionsMenu1080.swf"];
                        } else if (id == 5) {
                            // Exit Game. iOS apps normally shouldn't
                            // self-terminate (App Store rejects it) but
                            // this is a sideloaded dev build; mirrors the
                            // console LCE where Exit Game fully quits.
                            // Briefly delay so the press animation shows.
                            dispatch_after(
                                dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)),
                                dispatch_get_main_queue(), ^{
                                    NSLog(@"[MinecraftVC] Exit Game pressed, terminating");
                                    mcle_audio_stop_menu_music();
                                    exit(0);
                                });
                        }
                    }
                }
            }
            // B button (code=1, mask 0x00000002) -> back to MainMenu when
            // we're on any non-MainMenu scene. Mirrors the console's
            // "Cancel" handler on submenus.
            if ((pressedNow & 0x00000002u)
                && ![self.currentMenuSwf isEqualToString:@"MainMenu1080.swf"]) {
                NSLog(@"[MinecraftVC] back -> MainMenu1080.swf from %@",
                      self.currentMenuSwf);
                [self transitionToMenuNamed:@"MainMenu1080.swf"];
            }
        }

        // Ruffle owns the frame: advance the player, wgpu draws + presents.
        ruffle_ios_player_tick(g_ruffle_player, 1.0f / 60.0f);
    } else {
        // Legacy Metal path (triangle + pink rect).
        mcle_render_frame();
    }

    const char* swfStatusC = mcle_swf_last_status();
    NSString* swfStatus = swfStatusC ? [NSString stringWithUTF8String:swfStatusC] : @"";
    int swfReady = mcle_swf_is_ready();
    int swfHas = mcle_swf_has_movie();
    unsigned long long swfFrames = mcle_swf_total_frames();
    unsigned long long swfStrips = mcle_swf_total_mesh_strips();
    unsigned long long swfTris   = mcle_swf_total_triangles();

    unsigned long long swfBitmaps = mcle_swf_total_bitmap_draws();
    unsigned long long swfLines   = mcle_swf_total_line_strips();
    unsigned long long swfMasks   = mcle_swf_total_masks();
    unsigned long long swfFillBmp = mcle_swf_total_fill_bitmaps();

    int rustMagic = ruffle_ios_magic();
    int renderProbe = ruffle_ios_render_probe();
    extern int g_ruffle_swf_version;
    extern int g_ruffle_player_ok;
    extern int g_ruffle_framerate_mHz;

    int wgpuProbe = ruffle_ios_wgpu_probe();
    extern int g_ruffle_surface_probe;

    // Pull the ExternalInterface call log Ruffle has captured so far.
    static uint8_t extintBuf[4096];
    size_t extintLen = ruffle_ios_extint_log(extintBuf, sizeof(extintBuf));
    NSString* extintLog;
    if (extintLen) {
        NSString* full = [[NSString alloc] initWithBytes:extintBuf
                                                   length:extintLen
                                                 encoding:NSUTF8StringEncoding];
        NSArray<NSString*>* lines = [full componentsSeparatedByString:@"\n"];
        const NSUInteger kMaxLines = 10;
        if (lines.count > kMaxLines) {
            lines = [lines subarrayWithRange:
                NSMakeRange(lines.count - kMaxLines, kMaxLines)];
        }
        extintLog = [lines componentsJoinedByString:@"\n"];
    } else {
        extintLog = @"<no ExternalInterface calls yet>";
    }

    // Ruffle's AVM trace/warning output. Rust returns the tail of the
    // ring buffer; we then keep only the last 10 lines so the label's
    // fixed frame on iPhone doesn't clip the newest events off the
    // bottom of the screen.
    static uint8_t avmBuf[16384];
    size_t avmLen = ruffle_ios_avm_log(avmBuf, sizeof(avmBuf));
    NSString* avmLog;
    if (avmLen) {
        NSString* full = [[NSString alloc] initWithBytes:avmBuf
                                                   length:avmLen
                                                 encoding:NSUTF8StringEncoding];
        // Collapse noisy prefixes so a single log entry doesn't wrap
        // across 3 visual rows on the phone. Container UUID changes
        // per install, so match by structure.
        NSArray<NSArray<NSString*>*>* subs = @[
            @[@"file:///private/var/mobile/Containers/Data/Application/[0-9A-F-]+/Documents/",
              @"Docs/"],
            @[@"trc \\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d+Z\\s+", @""],
            @[@"ruffle_core::", @""],
        ];
        for (NSArray<NSString*>* pair in subs) {
            NSError* err = nil;
            NSRegularExpression* rx = [NSRegularExpression
                regularExpressionWithPattern:pair[0]
                options:0
                error:&err];
            if (rx) {
                full = [rx stringByReplacingMatchesInString:full
                    options:0
                    range:NSMakeRange(0, full.length)
                    withTemplate:pair[1]];
            }
        }
        NSArray<NSString*>* allLines = [full componentsSeparatedByString:@"\n"];
        // Filter out the Mojangles7 font noise. It's a known
        // outstanding issue and spams the ring, hiding the
        // preload/drain cascade diagnostics we actually need.
        NSMutableArray<NSString*>* filtered = [NSMutableArray new];
        NSUInteger droppedFallback = 0;
        for (NSString* line in allLines) {
            // Collapse the repeating "Fallback font not found (Sans)" spam
            // (one line per text field per frame) into a count so the real
            // device-font load result isn't pushed off the bottom.
            if ([line containsString:@"Fallback font not found"]) {
                droppedFallback++;
                continue;
            }
            [filtered addObject:line];
        }
        if (droppedFallback > 0) {
            [filtered addObject:[NSString stringWithFormat:
                @"(suppressed %lu 'Fallback font not found' lines)",
                (unsigned long)droppedFallback]];
        }
        const NSUInteger kTail = 8;
        if (filtered.count > kTail) {
            filtered = [[filtered subarrayWithRange:
                NSMakeRange(filtered.count - kTail, kTail)] mutableCopy];
        }
        avmLog = [filtered componentsJoinedByString:@"\n"];
    } else {
        avmLog = @"<no AVM output>";
    }

    int curFrame = g_ruffle_player ? ruffle_ios_player_current_frame(g_ruffle_player) : -99;
    int movW = g_ruffle_player ? ruffle_ios_player_movie_width(g_ruffle_player) : -99;
    int movH = g_ruffle_player ? ruffle_ios_player_movie_height(g_ruffle_player) : -99;
    uint64_t tickN = ruffle_ios_tick_count();

    uint64_t stage[5] = {0, 0, 0, 0, 0};
    int playingSample = 0;
    ruffle_ios_player_diag(stage, 5, &playingSample, g_ruffle_player);
    const char* playingStr = (playingSample == 1) ? "true"
                          : (playingSample == 2) ? "false"
                          : "?";
    int cfPre = -1, cfMid = -1, cfPost = -1;
    uint64_t frameAdvances = 0;
    ruffle_ios_player_frame_diag(&cfPre, &cfMid, &cfPost, &frameAdvances);

    int burnDone = 0, burnFirst = 0, burnFinal = 0, burnMax = 0, burnUnique = 0;
    ruffle_ios_burn_diag(&burnDone, &burnFirst, &burnFinal, &burnMax, &burnUnique);

    uint64_t avmTraces = 0, avmWarns = 0;
    ruffle_ios_avm_counts(&avmTraces, &avmWarns);

    NSString* swfLine = [NSString stringWithFormat:
        @"cur_frame=%d  movie=%dx%d  ticks=%llu  is_playing=%s\n"
        @"stages  lock=%llu tick=%llu run=%llu render=%llu  exec=%llu\n"
        @"frame pre/mid/post=%d/%d/%d  advances=%llu  burn first=%d final=%d max=%d\n"
        @"wgpu=%d(%s)  surface=%d  mov_parse_v=%d  traces=%llu  warns=%llu\n"
        @"Loaded SWF: %@\n"
        @"font: %@\n"
        @"audio: %@\n"
        @"--- ExtInt (calls + addCallback names) ---\n%@\n"
        @"--- AVM log (latest 8) ---\n%@",
        curFrame, movW, movH, tickN, playingStr,
        stage[0], stage[1], stage[2], stage[3], stage[4],
        cfPre, cfMid, cfPost, frameAdvances, burnFirst, burnFinal, burnMax,
        wgpuProbe,
        (wgpuProbe == 1 ? "OK" :
         wgpuProbe == -1 ? "no adapter" :
         wgpuProbe == -2 ? "no device" : "??"),
        g_ruffle_surface_probe,
        g_ruffle_swf_version,
        avmTraces,
        avmWarns,
        self.loadedSwfName ?: @"<none>",
        self.fontStatus ?: @"<not probed>",
        ({
            char audBuf[256] = {0};
            mcle_audio_status(audBuf, sizeof(audBuf));
            [NSString stringWithUTF8String:audBuf] ?: @"<unknown>";
        }),
        extintLog,
        avmLog];
    // Drop-on-floor for readouts we pulled but no longer surface on screen.
    (void)rustMagic; (void)renderProbe;
    (void)burnDone; (void)burnUnique;
    (void)g_ruffle_player_ok; (void)g_ruffle_framerate_mHz;
    (void)swfReady; (void)swfHas; (void)swfFrames; (void)swfStatus;
    (void)swfStrips; (void)swfTris;
    (void)swfBitmaps; (void)swfLines; (void)swfMasks; (void)swfFillBmp;

    mcle_ios_pad_state pad;
    if (mcle_ios_input_poll(0, &pad)) {
        self.statusLabel.text = [NSString stringWithFormat:
            @"Minecraft: Legacy Console Edition (iOS)\n\n"
            @"%@\n\n"
            @"Controller connected.\n"
            @"buttons: 0x%08X\n"
            @"L stick: %+.2f, %+.2f\n"
            @"R stick: %+.2f, %+.2f\n"
            @"triggers: L=%.2f R=%.2f",
            swfLine,
            pad.buttons, pad.lx, pad.ly, pad.rx, pad.ry, pad.lt, pad.rt];
    } else {
        self.statusLabel.text = [NSString stringWithFormat:
            @"Minecraft: Legacy Console Edition (iOS)\n\n"
            @"Early scaffold build. Renderer: Metal.\n"
            @"%@\n"
            @"Connect a controller to see input events.\n",
            swfLine];
    }
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

@end
