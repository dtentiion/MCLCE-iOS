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
@end

@implementation MinecraftViewController

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
    self.statusLabel.textColor = UIColor.whiteColor;
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
    mcle_render_init((__bridge void*)self.metalView.layer, pw, ph);

    // Probe: can Rust's wgpu create a Metal surface attached to our layer?
    // This is the last runtime unknown for wiring ruffle_render_wgpu.
    extern int g_ruffle_surface_probe;
    g_ruffle_surface_probe =
        ruffle_ios_surface_probe((__bridge void*)self.metalView.layer);
    NSLog(@"[MinecraftVC] ruffle_ios_surface_probe -> %d",
          g_ruffle_surface_probe);

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
    mcle_render_frame();

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

    NSString* swfLine = [NSString stringWithFormat:
        @"Ruffle magic: 0x%08X  render_probe=%d\n"
        @"wgpu on Metal: %s (%d)  surface_on_CAMetalLayer=%d\n"
        @"Ruffle SWF parse of test_rect.swf -> v%d (want 6)\n"
        @"Ruffle Player: %s  declared_fps=%.3f\n"
        @"GameSWF (legacy): ready=%d movie=%d frames=%llu\n"
        @"  %@",
        rustMagic, renderProbe,
        (wgpuProbe == 1 ? "OK" :
         wgpuProbe == -1 ? "no adapter" :
         wgpuProbe == -2 ? "no device" : "??"),
        wgpuProbe,
        g_ruffle_surface_probe,
        g_ruffle_swf_version,
        (g_ruffle_player_ok == 1 ? "INSTANTIATED" :
         g_ruffle_player_ok == 0 ? "FAILED" : "not probed"),
        g_ruffle_framerate_mHz / 1000.0,
        swfReady, swfHas, swfFrames,
        swfStatus];
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
