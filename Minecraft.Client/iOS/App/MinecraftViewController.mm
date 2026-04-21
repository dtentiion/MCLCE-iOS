#import "MinecraftViewController.h"
#import "MetalView.h"

#import <QuartzCore/CADisplayLink.h>

#include "RND_iOS_Stub.h"
#include "INP_iOS_Bridge.h"
#include "mcle_swf_bridge.h"
#include "ruffle_ios.h"

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
@property (nonatomic) uint32_t lastPadButtons;          // last seen 4J button bitmask, for edge detection
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
    self.statusLabel.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.25];
    self.statusLabel.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
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
            g_ruffle_player = ruffle_ios_player_create_wgpu(
                (__bridge void*)self.metalView.layer,
                pw, ph,
                (const uint8_t*)data.bytes, data.length,
                docsPath.UTF8String);
            // Tag the origin: Documents/ path = user-supplied, else bundle.
            BOOL fromDocs = [swfPath rangeOfString:@"/Documents/"].location != NSNotFound;
            self.loadedSwfName = [NSString stringWithFormat:@"%@ (%@)",
                swfPath.lastPathComponent,
                fromDocs ? @"Documents" : @"bundled"];
            NSLog(@"[MinecraftVC] ruffle wgpu player = %p  (loaded %@)",
                  g_ruffle_player, self.loadedSwfName);
        }
    }
    if (!self.loadedSwfName) self.loadedSwfName = @"<none>";

    if (!g_ruffle_player) {
        // Fallback: our hand-rolled Metal pipeline drew the triangle before.
        mcle_render_init((__bridge void*)self.metalView.layer, pw, ph);
    }

    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
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
        NSArray<NSString*>* lines = [full componentsSeparatedByString:@"\n"];
        // Show the first kHead lines (startup import cascade). Tail is
        // almost always heartbeats right now; skipping it gives us more
        // room for the diagnostics that actually matter on a phone
        // that only lets us scroll the overlay by a single line.
        const NSUInteger kHead = 25;
        if (lines.count > kHead) {
            lines = [lines subarrayWithRange:NSMakeRange(0, kHead)];
        }
        avmLog = [lines componentsJoinedByString:@"\n"];
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

    NSString* swfLine = [NSString stringWithFormat:
        @"cur_frame=%d  movie=%dx%d  ticks=%llu  is_playing=%s\n"
        @"stages  lock=%llu tick=%llu run=%llu render=%llu  exec=%llu\n"
        @"frame pre/mid/post=%d/%d/%d  advances=%llu  burn first=%d final=%d max=%d\n"
        @"wgpu=%d(%s)  surface=%d  mov_parse_v=%d\n"
        @"Loaded SWF: %@\n"
        @"--- ExtInt (calls + addCallback names) ---\n%@\n"
        @"--- AVM log (startup, first 25) ---\n%@",
        curFrame, movW, movH, tickN, playingStr,
        stage[0], stage[1], stage[2], stage[3], stage[4],
        cfPre, cfMid, cfPost, frameAdvances, burnFirst, burnFinal, burnMax,
        wgpuProbe,
        (wgpuProbe == 1 ? "OK" :
         wgpuProbe == -1 ? "no adapter" :
         wgpuProbe == -2 ? "no device" : "??"),
        g_ruffle_surface_probe,
        g_ruffle_swf_version,
        self.loadedSwfName ?: @"<none>",
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
