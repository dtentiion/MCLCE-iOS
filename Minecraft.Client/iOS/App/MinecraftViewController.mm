#import "MinecraftViewController.h"
#import "MetalView.h"

#import <QuartzCore/CADisplayLink.h>
#import <CoreText/CoreText.h>

#include "RND_iOS_Stub.h"
#include "INP_iOS_Bridge.h"
#include "mcle_swf_bridge.h"
#include "ruffle_ios.h"
#include "MCLE_iOS_Audio.h"
#include "MCLE_iOS_Settings.h"

extern "C" void mcle_game_tick(void);  // GameBootstrap.cpp

// Name of the currently loaded menu SWF, set whenever we enter a
// new scene. The settings-event bridge reads this to decide which
// scene's control-id -> MCLE_SETTING_* mapping to use, without
// holding a reference to the view controller itself.
static NSString* g_current_scene_name = @"";

// Pending dialog request: seeded by presentDialog..., consumed by
// populateMessageBoxSibling once MessageBox1080.swf is attached as
// a stage sibling, fired by the extint bridge on button press or
// by the B-back handler as a cancellation. While this is non-nil
// the iOS-side menu state machine is suppressed and B / handlePress
// route through the dialog instead of the underlying scene.
// Mirrors console's MessageBoxInfo that
// UIController::RequestMessageBox stashes when navigating to
// eUIScene_MessageBox (UIController.cpp:2951-2994).
//
// Result codes track C4JStorage::EMessageResult used in every
// console dialog callback signature. ControlId 0..3 from the
// MessageBox scene's handlePress map to Accept, Decline, Third,
// Fourth respectively (UIScene_MessageBox.cpp:120-141). -1 is
// Cancelled (B press, matches EMessage_Cancelled).
typedef NS_ENUM(NSInteger, MCLEDialogResult) {
    MCLEDialogResultCancelled  = -1,
    MCLEDialogResultAccept     =  0,
    MCLEDialogResultDecline    =  1,
    MCLEDialogResultThird      =  2,
    MCLEDialogResultFourth     =  3,
};
typedef void (^MCLEDialogCallback)(MCLEDialogResult result);

@interface MCLEDialogRequest : NSObject
@property (nonatomic, copy)   NSString* title;
@property (nonatomic, copy)   NSString* content;
@property (nonatomic, copy)   NSArray<NSString*>* buttonLabels;  // filled end-first per MessageBox.as:47
@property (nonatomic, assign) int focusIndex;
@property (nonatomic, copy)   MCLEDialogCallback callback;
@end
@implementation MCLEDialogRequest
@end

static MCLEDialogRequest* g_pending_dialog = nil;

// Stage depth used for the MessageBox sibling overlay. Sits in
// front of the underlying scene (depth 0), the Tooltips strip
// (100), and the Logo (101). The dim layer at depth -1 is behind
// all of them. Console picks no specific depth (Iggy stack is
// scene-swap-only on console); we own the depth choice here.
static const int kMessageBoxDepth = 200;

// Weak-ish ref to the live VC so C-style ExternalInterface bridge
// callbacks can reach instance methods (navigateBack,
// finishDialogWithResult:) without plumbing self through the C ABI.
// Set in viewDidLoad, cleared in viewDidDisappear. The full
// MinecraftViewController @interface lives further down in this
// file, so forward-declare the class and a category that surfaces
// just the methods the bridge needs to call. The class-extension
// body below adopts these.
@class MinecraftViewController;
@interface MinecraftViewController (MCLEBridgeEntryPoints)
- (void)finishDialogWithResult:(MCLEDialogResult)result;
- (void)navigateBack;
@end
static __weak MinecraftViewController* g_active_vc = nil;

// Maps an (sliderId, rawValue) coming from handleSliderMove in
// whatever scene g_current_scene_name names, to a pair
// (mcle_setting index, value to store). Returns -1 if the pair
// doesn't map to anything persisted.
static int mcle_map_slider(int sliderId, double rawValue, unsigned char* outValue) {
    if (!outValue) return -1;
    int v = (int)rawValue;
    if (v < 0) v = 0; if (v > 255) v = 255;
    *outValue = (unsigned char)v;
    if ([g_current_scene_name isEqualToString:@"SettingsAudioMenu1080.swf"]) {
        if (sliderId == 0) return MCLE_SETTING_MusicVolume;
        if (sliderId == 1) return MCLE_SETTING_SoundFXVolume;
    } else if ([g_current_scene_name isEqualToString:@"SettingsControlMenu1080.swf"]) {
        if (sliderId == 0) return MCLE_SETTING_SensitivityInGame;
        if (sliderId == 1) return MCLE_SETTING_SensitivityInMenu;
    } else if ([g_current_scene_name isEqualToString:@"SettingsOptionsMenu1080.swf"]) {
        if (sliderId == 5) return MCLE_SETTING_Autosave;
        if (sliderId == 7) return MCLE_SETTING_Difficulty;
    } else if ([g_current_scene_name isEqualToString:@"SettingsGraphicsMenu1080.swf"]) {
        // RenderDistance slider value is a level 0..5; store the
        // block count the level maps to (matches console's
        // eGameSetting_RenderDistance which holds the count).
        if (sliderId == 3) {
            static const int kDistance[6] = {2, 4, 8, 16, 32, 64};
            int lvl = v; if (lvl < 0) lvl = 0; if (lvl > 5) lvl = 5;
            *outValue = (unsigned char)kDistance[lvl];
            return MCLE_SETTING_RenderDistance;
        }
        if (sliderId == 4) return MCLE_SETTING_Gamma;
        if (sliderId == 5) return MCLE_SETTING_FOV;
        if (sliderId == 6) return MCLE_SETTING_InterfaceOpacity;
    } else if ([g_current_scene_name isEqualToString:@"SettingsUIMenu1080.swf"]) {
        if (sliderId == 6) return MCLE_SETTING_UISize;
        if (sliderId == 7) return MCLE_SETTING_UISizeSplitscreen;
    }
    return -1;
}

static int mcle_map_checkbox(int boxId) {
    if ([g_current_scene_name isEqualToString:@"SettingsOptionsMenu1080.swf"]) {
        switch (boxId) {
            case 0: return MCLE_SETTING_ViewBob;
            case 1: return MCLE_SETTING_Hints;
            case 2: return MCLE_SETTING_Tooltips;
            case 3: return MCLE_SETTING_GamertagsVisible;
            // case 4: MashUp unhide - one-shot action, no store slot.
        }
    } else if ([g_current_scene_name isEqualToString:@"SettingsGraphicsMenu1080.swf"]) {
        switch (boxId) {
            case 0: return MCLE_SETTING_Clouds;
            case 1: return MCLE_SETTING_BedrockFog;
            case 2: return MCLE_SETTING_CustomSkinAnim;
        }
    } else if ([g_current_scene_name isEqualToString:@"SettingsUIMenu1080.swf"]) {
        switch (boxId) {
            case 0: return MCLE_SETTING_DisplayHUD;
            case 1: return MCLE_SETTING_DisplayHand;
            case 2: return MCLE_SETTING_DeathMessages;
            case 3: return MCLE_SETTING_AnimatedCharacter;
            case 4: return MCLE_SETTING_SplitScreenVertical;
            case 5: return MCLE_SETTING_DisplaySplitscreenGamertags;
        }
    }
    return -1;
}

extern "C" void mcle_ios_settings_event_bridge(const char* method, double id, double value) {
    if (!method) return;
    if (strcmp(method, "handleCheckboxToggled") == 0) {
        int setting = mcle_map_checkbox((int)id);
        if (setting < 0) return;
        unsigned char v = (value != 0.0) ? 1 : 0;
        mcle_settings_set(setting, v);
        NSLog(@"[settings] %@ checkbox id=%d -> setting=%d val=%u",
              g_current_scene_name, (int)id, setting, v);
    } else if (strcmp(method, "handleSliderMove") == 0) {
        unsigned char stored = 0;
        int setting = mcle_map_slider((int)id, value, &stored);
        if (setting < 0) return;
        mcle_settings_set(setting, stored);
        NSLog(@"[settings] %@ slider id=%d raw=%g -> setting=%d val=%u",
              g_current_scene_name, (int)id, value, setting, stored);

        // Apply live for the settings the audio engine cares about
        // so the Music / Sound sliders actually fade the playing
        // track rather than just writing to disk.
        if (setting == MCLE_SETTING_MusicVolume) {
            mcle_audio_set_music_volume(stored);
        } else if (setting == MCLE_SETTING_SoundFXVolume) {
            mcle_audio_set_sfx_volume(stored);
        }

        // Update the slider's displayed label. Console does this in
        // UIControl_Slider::handleSliderMove (line 108-111 of
        // UIControl_Slider.cpp) via setLabel with the format string
        // the scene registered. Mirror per-control here.
        extern PlayerHandle* g_ruffle_player;
        if (!g_ruffle_player) return;
        NSString* sliderName = nil;
        NSString* sliderLabel = nil;
        int raw = (int)value;
        if ([g_current_scene_name isEqualToString:@"SettingsAudioMenu1080.swf"]) {
            if ((int)id == 0) {
                sliderName = @"Music";
                sliderLabel = [NSString stringWithFormat:@"Music: %d%%", raw];
            } else if ((int)id == 1) {
                sliderName = @"Sound";
                sliderLabel = [NSString stringWithFormat:@"Sound: %d%%", raw];
            }
        } else if ([g_current_scene_name isEqualToString:@"SettingsControlMenu1080.swf"]) {
            if ((int)id == 0) {
                sliderName = @"SensitivityInGame";
                sliderLabel = [NSString stringWithFormat:@"Sensitivity In-game: %d%%", raw];
            } else if ((int)id == 1) {
                sliderName = @"SensitivityInMenu";
                sliderLabel = [NSString stringWithFormat:@"Sensitivity In-menu: %d%%", raw];
            }
        } else if ([g_current_scene_name isEqualToString:@"SettingsOptionsMenu1080.swf"]) {
            if ((int)id == 5) {
                sliderName = @"Autosave";
                sliderLabel = (raw == 0)
                    ? @"Autosave: Off"
                    : [NSString stringWithFormat:@"Autosave: %d minutes", raw * 15];
            } else if ((int)id == 7) {
                static NSArray<NSString*>* kDiff = @[@"Peaceful", @"Easy", @"Normal", @"Hard"];
                int d = MAX(0, MIN(raw, 3));
                sliderName = @"Difficulty";
                sliderLabel = [NSString stringWithFormat:@"Difficulty: %@", kDiff[d]];
            }
        } else if ([g_current_scene_name isEqualToString:@"SettingsGraphicsMenu1080.swf"]) {
            if ((int)id == 3) {
                static const int kDistance[6] = {2, 4, 8, 16, 32, 64};
                int lvl = MAX(0, MIN(raw, 5));
                sliderName = @"RenderDistance";
                sliderLabel = [NSString stringWithFormat:@"Render Distance: %d", kDistance[lvl]];
            } else if ((int)id == 4) {
                sliderName = @"Gamma";
                sliderLabel = [NSString stringWithFormat:@"Gamma: %d%%", raw];
            } else if ((int)id == 5) {
                int fovDeg = 70 + (raw * (110 - 70)) / 100;
                sliderName = @"FOV";
                sliderLabel = [NSString stringWithFormat:@"FOV: %d", fovDeg];
            } else if ((int)id == 6) {
                sliderName = @"InterfaceOpacity";
                sliderLabel = [NSString stringWithFormat:@"Interface opacity: %d%%", raw];
            }
        } else if ([g_current_scene_name isEqualToString:@"SettingsUIMenu1080.swf"]) {
            if ((int)id == 6) {
                sliderName = @"UISize";
                sliderLabel = [NSString stringWithFormat:@"UI Size: %d", raw];
            } else if ((int)id == 7) {
                sliderName = @"UISizeSplitscreen";
                sliderLabel = [NSString stringWithFormat:@"UI Size Split-screen: %d", raw];
            }
        }
        if (sliderName.length && sliderLabel.length) {
            // DO NOT call back into ruffle_ios_* from here: the
            // ExtInt callback path holds Player::lock already, and
            // call_init_on_named_child tries to re-acquire it,
            // deadlocking the whole game. Defer the SetLabel to
            // the next main-queue run so the player lock releases
            // first.
            NSString* capturedName = sliderName;
            NSString* capturedLabel = sliderLabel;
            dispatch_async(dispatch_get_main_queue(), ^{
                extern PlayerHandle* g_ruffle_player;
                if (!g_ruffle_player) return;
                const char* n = capturedName.UTF8String;
                const char* l = capturedLabel.UTF8String;
                ruffle_ios_call_init_on_named_child(
                    g_ruffle_player,
                    (const uint8_t*)n, strlen(n),
                    (const uint8_t*)"SetLabel", 8,
                    (const uint8_t*)l, strlen(l),
                    0.0);
            });
        }
    } else if (strcmp(method, "handlePress") == 0) {
        // LCE SWF fires handlePress(controlId, childId) when the
        // user hits A on a button or list item. FJ_Document's
        // stage-level handler in FJ_Document.as:170-191 routes
        // through ExternalInterface. For list widgets (LoadOrJoin
        // SavesList/JoinList) controlId is the FJ_ButtonList's id
        // and childId is the item index. For simple FJ_Button
        // screens the MainMenu path fires controlId=buttonId,
        // childId=0 - but the host's DPad handler already
        // routes simple-button scenes natively, so we only need
        // the list-widget path here.
        int listId = (int)id;
        int itemId = (int)value;
        if ([g_current_scene_name isEqualToString:@"LoadOrJoinMenu1080.swf"]) {
            if (listId == 0) {
                // SavesList. Item 0 = "Create New World" (see
                // UIScene_LoadOrJoinMenu.cpp JOIN_LOAD_CREATE_BUTTON_
                // INDEX=0); anything > 0 is a save slot or level
                // generator. Neither path is functional yet -
                // the create-world flow needs a level-gen picker
                // scene, and the load-save flow needs the LCE
                // save-format reader.
                NSLog(@"[loadorjoin] saves list press idx=%d (loading path is a follow-up)",
                      itemId);
            } else if (listId == 1) {
                // JoinList. iOS has no netcode yet, so this list
                // stays empty - log in case we get here anyway
                // so the event path is visible.
                NSLog(@"[loadorjoin] join list press idx=%d (no networking yet)",
                      itemId);
            }
        } else if ([g_current_scene_name isEqualToString:@"HowToPlayMenu1080.swf"]) {
            // HowToList item press. Console routes each topic to
            // its own tutorial scene via m_uiHTPSceneA
            // (UIScene_HowToPlayMenu.cpp:42-75 + handlePress at
            // line 230+). Those scenes are gameplay-facing pages
            // (UIScene_HowToPlay_Basics, _Inventory, etc.) that
            // will land once the gameplay layer is in; for now
            // pressing a topic just logs which one.
            NSLog(@"[howtoplay] topic press idx=%d (topic scenes are a follow-up)",
                  itemId);
        } else if ([g_current_scene_name isEqualToString:@"DLCMainMenu1080.swf"]) {
            // Console's UIScene_DLCMainMenu::handlePress opens
            // DLCOffersMenu for the selected offer category. Not
            // wired until the store backend lands.
            NSLog(@"[dlcmain] offer press idx=%d (store backend is a follow-up)",
                  itemId);
        } else if ([g_current_scene_name isEqualToString:@"LeaderboardMenu1080.swf"]) {
            // FJ_LeaderboardList is native-driven on console
            // (UIControl_LeaderboardList customDraw + per-row
            // selection). We don't populate the list yet so a
            // press here shouldn't fire, but log it anyway.
            NSLog(@"[leaderboard] entry press idx=%d (online service is a follow-up)",
                  itemId);
        } else if (g_pending_dialog) {
            // MessageBox button press. Mirrors
            // UIScene_MessageBox::handlePress (UIScene_MessageBox.cpp:120):
            //   controlId 0 -> EMessage_ResultAccept
            //   controlId 1 -> EMessage_ResultDecline
            //   controlId 2 -> EMessage_ResultThirdOption
            //   controlId 3 -> EMessage_ResultFourthOption
            // controlId reported is the button's id, which
            // populateMessageBoxSibling sets to the sequence
            // counter (0 for the first visible button, etc.).
            // With the sibling-overlay model g_current_scene_name
            // is still the underlying scene so we key off
            // g_pending_dialog instead.
            MCLEDialogResult r = MCLEDialogResultCancelled;
            switch (listId) {
                case 0: r = MCLEDialogResultAccept;   break;
                case 1: r = MCLEDialogResultDecline;  break;
                case 2: r = MCLEDialogResultThird;    break;
                case 3: r = MCLEDialogResultFourth;   break;
                default: break;
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                MinecraftViewController* vc = g_active_vc;
                if (vc) [vc finishDialogWithResult:r];
            });
        } else {
            NSLog(@"[handlePress] scene=%@ list=%d item=%d (unhandled)",
                  g_current_scene_name, listId, itemId);
        }
    } else if (strcmp(method, "handleInitFocus") == 0) {
        // LCE fires this once per scene when SetFocus(-1) finds
        // the tabIndex==1 child (FJ_Document.SetFocus line 81-160,
        // OnFocusChange(false) calls through to handleInitFocus).
        // Purely informational for now; logging helps verify the
        // SetFocus path is firing as expected.
        NSLog(@"[handleInitFocus] scene=%@ control=%d child=%d",
              g_current_scene_name, (int)id, (int)value);
    }
}
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
// Yellow rotating "splash text" next to the title logo. Matches the
// console CXuiCtrlSplashPulser (Common/XUI/XUI_Ctrl_SplashPulser.cpp):
// picks one line from splashes.txt at launch, draws it rotated -17
// degrees, pulses with sin(currentTimeMillis). Lives as a native
// UIKit overlay because on console the splash is drawn in native C++
// outside the Iggy movie too (the XUI entry is only a placeholder
// XuiLabel with ClassOverride CXuiCtrlSplashPulser, and OnRender
// draws via raw GL, not through the SWF).
@property (strong, nonatomic) UILabel* splashLabel;
@property (strong, nonatomic) NSString* splashText;
// All lines of splashes.txt cached at launch. pickSplash rolls a
// fresh entry each time MainMenu becomes the current scene, matching
// UIScene_MainMenu::UIScene_MainMenu on console where the splash is
// re-picked on every construction.
@property (strong, nonatomic) NSArray<NSString*>* splashes;
// Ordered stack of scene entries the player navigated through,
// oldest at index 0. Each entry is a dict with @"swf" (NSString)
// and @"focus" (NSNumber int) so B-back restores both the scene
// and which button the user had selected before going deeper.
// Mirrors how UIController::NavigateBack walks the scene group
// stack on console (Common/UI/UIController.cpp:2006) without
// porting the full UIGroup machinery.
@property (strong, nonatomic) NSMutableArray<NSDictionary*>* menuStack;
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

// Mirror console's UIController::loadSkin aliasing (UIController.cpp
// 540-611), where the platform-specific Windows skin SWF
// (skinHDWin.swf) is loaded under the alias platformskinHD.swf so
// scene SWFs that reference platformskinHD.swf find the actual
// file. Per-platform variants exist (skinHDDurango on XB1,
// skinHDOrbis on PS4, skinHDWin on Windows64). For iOS we fall
// back to the Windows64 variant since that is what the
// MediaWindows64.arc dump in INSTALL.md walks the user through
// extracting.
//
// Symlinks aren't allowed inside the app sandbox without
// entitlements, so make a real copy. Only runs once: skips if
// the alias name already exists.
+ (void)installPlatformSkinAliasIfNeeded {
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (!docs) return;
    NSFileManager* fm = NSFileManager.defaultManager;
    struct AliasPair { NSString* alias; NSArray<NSString*>* sources; };
    AliasPair pairs[] = {
        { @"platformskinHD.swf", @[ @"skinHDWin.swf", @"skinHDDurango.swf",
                                    @"skinHDOrbis.swf", @"skinHDWiiU.swf" ] },
        { @"platformskin.swf",   @[ @"skinWin.swf", @"skinPS3.swf",
                                    @"skinVita.swf" ] },
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        NSString* aliasPath = [docs stringByAppendingPathComponent:pairs[i].alias];
        if ([fm fileExistsAtPath:aliasPath]) continue;
        for (NSString* candidate in pairs[i].sources) {
            NSString* srcPath = [docs stringByAppendingPathComponent:candidate];
            if ([fm fileExistsAtPath:srcPath]) {
                NSError* err = nil;
                if ([fm copyItemAtPath:srcPath toPath:aliasPath error:&err]) {
                    NSLog(@"[platformskin] aliased %@ -> %@",
                          candidate, pairs[i].alias);
                } else {
                    NSLog(@"[platformskin] copy %@ -> %@ failed: %@",
                          candidate, pairs[i].alias, err);
                }
                break;
            }
        }
    }
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;

    // Lay down the platform-skin alias before any SWF parses
    // imports. Without this, ToolTips1080.swf (and any other
    // 1080p scene that imports platformskinHD.swf) 404s on the
    // imports for controller-button glyphs and the icons stay
    // blank.
    [MinecraftViewController installPlatformSkinAliasIfNeeded];

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

    // Cache splashes.txt at launch. File ships alongside the SWFs
    // in Documents. Each transition into MainMenu re-rolls via
    // pickSplash.
    NSString* docsDir = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    NSString* splashesPath = [docsDir stringByAppendingPathComponent:@"splashes.txt"];
    NSString* contents = [NSString stringWithContentsOfFile:splashesPath
                                                   encoding:NSUTF8StringEncoding
                                                      error:nil];
    if (contents.length) {
        self.splashes = [[contents componentsSeparatedByCharactersInSet:
                          [NSCharacterSet newlineCharacterSet]]
                         filteredArrayUsingPredicate:
                         [NSPredicate predicateWithFormat:@"length > 0"]];
    }
    [self pickSplash];

    // Register the Mojangles TTF with CoreText so UIFont can find it
    // by name. We already load it for Ruffle via ruffle_ios_stage_
    // device_font; UIKit has its own font registry that needs the
    // file separately. Font file is Mojang Font_7.ttf in Documents.
    NSString* fontPath = [docsDir stringByAppendingPathComponent:@"Mojang Font_7.ttf"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:fontPath]) {
        CFURLRef url = (__bridge_retained CFURLRef)[NSURL fileURLWithPath:fontPath];
        CFErrorRef err = NULL;
        CTFontManagerRegisterFontsForURL(url, kCTFontManagerScopeProcess, &err);
        if (err) { CFRelease(err); }
        CFRelease(url);
    }

    // Splash label: yellow, rotated -17 degrees, positioned next to
    // the title logo. Authored XUI position is (612, 126) on the
    // 1920x1080 stage; we convert to view points in the tick method
    // using self.view.bounds so the stage fit math stays dynamic.
    self.splashLabel = [[UILabel alloc] init];
    self.splashLabel.text = self.splashText;
    self.splashLabel.textColor = [UIColor colorWithRed:1.0 green:1.0
                                                  blue:0.0 alpha:1.0];
    self.splashLabel.font = [UIFont fontWithName:@"Mojangles7" size:18]
        ?: [UIFont fontWithName:@"Mojangles 7" size:18]
        ?: [UIFont boldSystemFontOfSize:18];
    self.splashLabel.backgroundColor = UIColor.clearColor;
    self.splashLabel.textAlignment = NSTextAlignmentCenter;
    self.splashLabel.shadowColor = [UIColor colorWithWhite:0 alpha:0.8];
    self.splashLabel.shadowOffset = CGSizeMake(1, 1);
    [self.splashLabel sizeToFit];
    [self.view addSubview:self.splashLabel];
    NSLog(@"[MinecraftVC] splash font=%@ text=%@",
          self.splashLabel.font.fontName, self.splashText);
}

// Pick a fresh splash string. Mirrors UIScene_MainMenu.cpp
// lines 195-225 on console: random pool starts at eSplashRandomStart
// + 1 (index 5, skipping the Happy-Birthday-ex / Notch / Xmas /
// NewYear overrides plus the "Hobo humping" line legal removed).
// Date overrides replace the roll on specific days.
- (void)pickSplash {
    NSArray<NSString*>* lines = self.splashes;
    NSString* picked = nil;
    if (lines.count) {
        NSDateComponents* dc = [[NSCalendar currentCalendar]
            components:NSCalendarUnitMonth | NSCalendarUnitDay
              fromDate:[NSDate date]];
        NSInteger m = dc.month, d = dc.day;
        if (m == 11 && d == 9 && lines.count > 0) {
            picked = lines[0];                 // Happy birthday, ez!
        } else if (m == 6 && d == 1 && lines.count > 1) {
            picked = lines[1];                 // Happy birthday, Notch!
        } else if (m == 12 && d == 24 && lines.count > 2) {
            picked = lines[2];                 // Merry X-mas!
        } else if (m == 1 && d == 1 && lines.count > 3) {
            picked = lines[3];                 // Happy New Year!
        } else if (lines.count > 5) {
            // Random pool excludes indexes 0..4.
            NSUInteger poolCount = lines.count - 5;
            NSUInteger idx = 5 + arc4random_uniform((uint32_t)poolCount);
            picked = lines[idx];
        } else {
            picked = lines[0];
        }
    }
    if (!picked.length) picked = @"Now Java-free!";
    self.splashText = picked;
    if (self.splashLabel) {
        self.splashLabel.text = picked;
        [self.splashLabel sizeToFit];
    }
    NSLog(@"[MinecraftVC] splash -> %@", picked);
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
            g_current_scene_name = self.currentMenuSwf ?: @"";
            self.menuStack = [NSMutableArray array];
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

                // The transitionToMenuNamed dispatcher seeds tooltips
                // on every navigation, but the very first scene
                // boots through ruffle_ios_player_create_wgpu without
                // going through that path. Replicate the per-scene
                // tooltip seed here so MainMenu's bottom strip shows
                // on the launch screen rather than waiting for the
                // first nav. MainMenu uses A=Select only per
                // UIScene_MainMenu::updateTooltips
                // (UIScene_MainMenu.cpp:124-142).
                [self seedTooltipsForScene:self.currentMenuSwf hidden:NO];
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
    // Preload the six UI SFX oggs so menu interactions are audible.
    // Files ship in Documents/UI/<name>.ogg, same layout as console's
    // Windows64Media/Sound/Minecraft/UI folder.
    mcle_audio_load_ui_sfx();
    // Load persisted game settings (or seed console defaults on
    // first run). Mirrors CMinecraftApp::InitGameSettings path:
    // load settings.dat, fall through to defaults otherwise.
    mcle_settings_load();
    // Seed live audio volumes from the settings store so first-run
    // or a pre-existing settings.dat applies immediately.
    mcle_audio_set_music_volume(mcle_settings_get(MCLE_SETTING_MusicVolume));
    mcle_audio_set_sfx_volume(mcle_settings_get(MCLE_SETTING_SoundFXVolume));
    // Suppress Ruffle's auto-drawn yellow focus rectangle.
    // LCE's FJ_Slider and FJ_CheckBox include their own authored
    // outline MovieClips (FJ_Slider_Outline etc) so the default
    // Flash Player focus indicator draws on top of the authored
    // one, producing a double yellow frame.
    ruffle_ios_suppress_auto_focus_highlight(g_ruffle_player, 1);
    // Register the AS3-side event bridge. ExternalInterface calls
    // to handleCheckboxToggled / handleSliderMove route through
    // this callback, which maps (current scene, control id) to an
    // MCLE_SETTING_* index and writes the value through to
    // settings.dat. Mirrors the per-scene handleCheckboxToggled /
    // handleSliderMove switch statements on console
    // (e.g. UIScene_SettingsOptionsMenu.cpp:379).
    extern void mcle_ios_settings_event_bridge(const char*, double, double);
    ruffle_ios_set_settings_event_callback(&mcle_ios_settings_event_bridge);

    g_active_vc = self;
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
            // Depth order (back to front): Panorama (-2),
            // MenuBackground (-1, dim), scene root (0), Tooltips
            // (100), Logo (101). The dim needs to sit BELOW the
            // scene root so it doesn't cover the dialog; stage
            // child depths are integers so Panorama drops to -2
            // to free -1 for MenuBackground.
            @[@"Panorama1080.swf",     @(-2),  @(1.5f), @(1.5f), @(-208.6f), @(0.0f)],
            // MenuBackground is the dim backdrop console adds via
            // UIComponent_MenuBackground while a MessageBox dialog
            // is up (UIScene_MessageBox.cpp:49 addComponent).
            // Authored 1080-wide; without stretching to match the
            // Panorama scale it only covered the centre band of
            // the screen. Same 1.5x and pre-shift Panorama uses so
            // it fills iPhone aspect ratios end-to-end.
            @[@"MenuBackground1080.swf",@(-1), @(1.5f), @(1.5f), @(-208.6f), @(0.0f)],
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

        // Hide the MenuBackground sibling at startup. Only shown
        // while a MessageBox dialog is up so it doesn't dim the
        // regular menu tree.
        ruffle_ios_set_xui_sibling_visible_at_depth(
            g_ruffle_player, -1, 0);

        // Tick the player a few frames so each sibling SWF's
        // ADDED_TO_STAGE event fires and its FJ_Document.init
        // runs (m_aToolTips array gets built etc.). Without this,
        // the very first seedTooltipsForScene call after
        // attachMenuScenery hits a Tooltips sibling whose init
        // hasn't run yet, SetToolTip silently no-ops on the
        // half-constructed clip, and the bottom-strip stays empty
        // until the next scene transition lets init catch up.
        // Subsequent scene transitions are fine because the
        // sibling's already constructed by then.
        for (int i = 0; i < 10; ++i) {
            ruffle_ios_player_tick_headless(g_ruffle_player, 1.0f / 60.0f);
        }
    }
}

// Configure the tooltips strip at the bottom of the screen for
// the current scene. Drives ToolTips1080.swf (attached as a
// stage sibling at depth 100) via its public AS3 API:
//   SetToolTip(buttonId, label, show)  per ToolTips.as:144
//   UpdateLayout()                     per ToolTips.as:150
// Button ids match the TOOLTIP_BUTTON_* constants in ToolTips.as:
//   0 = A, 1 = B, 2 = X, 3 = Y, 4 = LT, 5 = RT, 6 = LB, 7 = RB,
//   8 = LS, 9 = RS, 10 = SELECT.
// Console seeds these per-scene via UIScene::updateTooltips,
// which most scenes implement as
// ui.SetTooltips(iPad, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK)
// and route through UIController -> UIComponent_Tooltips ->
// SetToolTip on each visible button.
//
// `hidden` is true for scenes where the tooltips strip would
// overlap a dialog or full-screen UI (e.g. MessageBox).
// Per-scene logo visibility. Mirrors which console UIScenes pass
// false to UILayer::showComponent(eUIComponent_Logo) in their
// updateComponents() override (SkinSelectMenu.cpp:143,
// LeaderboardsMenu.cpp:150, etc.). All other scenes leave logo on.
// Also called by finishDialogWithResult: after a dialog dismisses
// to put the logo back where the underlying scene wants it.
- (void)applyLogoVisibilityForScene:(NSString*)swfName {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    NSSet<NSString*>* logoOff = [NSSet setWithArray:@[
        @"SkinSelectMenu1080.swf",
        @"LeaderboardMenu1080.swf",
        @"LeaderboardsMenu1080.swf",
        @"DLCMainMenu1080.swf",
        @"CreateWorldMenu1080.swf",
        @"LaunchMoreOptionsMenu1080.swf",
    ]];
    int visible = [logoOff containsObject:swfName] ? 0 : 1;
    // Logo is depth 101 on our stage (see attachMenuScenery).
    ruffle_ios_set_xui_sibling_visible_at_depth(
        g_ruffle_player, 101, visible);
}

- (void)seedTooltipsForScene:(NSString*)swfName hidden:(BOOL)hidden {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    // Per-scene tooltip parity. Each console UIScene::updateTooltips
    // sets a different (A, B) pair via ui.SetTooltips(pad, iA, iB).
    // -1 means hide that slot. Match those exact pairs here.
    NSString* aLabel = @"Select";
    NSString* bLabel = @"Back";
    BOOL showA = YES;
    BOOL showB = YES;

    if ([swfName isEqualToString:@"MainMenu1080.swf"]) {
        // UIScene_MainMenu::updateTooltips
        // (UIScene_MainMenu.cpp:124-142): SetTooltips(pad, iA=Select,
        // iB=-1, iX=-1) - main menu only shows Select. No Back since
        // there's nowhere to back out to.
        showB = NO;
        bLabel = @"";
    } else if ([swfName isEqualToString:@"MessageBox1080.swf"]) {
        // UIScene_MessageBox::updateTooltips
        // (UIScene_MessageBox.cpp:74-77): SetTooltips(..., iA=Select,
        // iB=Cancel). Dialog cancels rather than backs.
        bLabel = @"Cancel";
    }
    // Every other scene (HelpAndOptions / Settings / LoadOrJoin /
    // HowToPlay / Leaderboard / DLCMain / SkinSelect) inherits the
    // default A=Select / B=Back pair, matching their respective
    // updateTooltips overrides on console.

    // SetToolTip signature: (int buttonId, String label, Boolean show)
    // Index 0..10 per ToolTips.as TOOLTIP_BUTTON_* constants.
    // Empty label + show=false collapses that slot in UpdateLayout.
    struct Slot { int id; const char* label; BOOL show; };
    Slot slots[] = {
        { 0,  aLabel.UTF8String, showA },   // A
        { 1,  bLabel.UTF8String, showB },   // B
        { 2,  "",                NO },      // X
        { 3,  "",                NO },      // Y
        { 4,  "",                NO },      // LT
        { 5,  "",                NO },      // RT
        { 6,  "",                NO },      // LB
        { 7,  "",                NO },      // RB
        { 8,  "",                NO },      // LS
        { 9,  "",                NO },      // RS
        { 10, "",                NO },      // SELECT
    };
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
        ruffle_ios_call_set_tooltip(
            g_ruffle_player, 100,
            slots[i].id,
            (const uint8_t*)slots[i].label, strlen(slots[i].label),
            slots[i].show ? 1 : 0);
    }

    // Push the strip in from the screen edge. Console drives this
    // via UIScene::updateSafeZone calling FJ_Document.SetSafeZone(
    // top, bottom, left, right) at UIScene.cpp:200-232. Defaults
    // give a generous bottom-left inset so the strip doesn't sit
    // flush against the edge.
    const char* setSafe = "SetSafeZone";
    double safe[4] = { 0.0, 60.0, 60.0, 0.0 }; // top, bottom, left, right
    ruffle_ios_call_method_on_sibling_root(g_ruffle_player, 100,
        (const uint8_t*)setSafe, strlen(setSafe),
        safe, 4);

    // Reposition visible tooltips along the bottom; collapsed
    // (show=false) slots end up off-screen so they don't take
    // up space in the strip.
    const char* updateName = "UpdateLayout";
    ruffle_ios_call_method_on_sibling_root(g_ruffle_player, 100,
        (const uint8_t*)updateName, strlen(updateName),
        NULL, 0);
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

// Mark self.menuFocusIndex as SELECTED on the buttons of the
// current menu, everything else UNSELECTED. Used after
// navigateBack so the focused button is the one the user left on,
// not always Button1. State codes 1=SELECTED, 2=UNSELECTED match
// the console FJ_Button states we drive from the input handler.
- (void)refreshFocusState {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player || !self.menuButtonConfig.count) return;
    int focus = self.menuFocusIndex;
    for (NSUInteger i = 0; i < self.menuButtonConfig.count; ++i) {
        const char* name = [self.menuButtonConfig[i][@"name"] UTF8String];
        int state = ((int)i == focus) ? 1 : 2;
        ruffle_ios_call_init_on_named_child(
            g_ruffle_player,
            (const uint8_t*)name, strlen(name),
            (const uint8_t*)"ChangeState", 11,
            (const uint8_t*)"", 0,
            (double)state);
    }
}

// First-pass port of UIScene_SettingsOptionsMenu's init block
// (Common/UI/UIScene_SettingsOptionsMenu.cpp:25). Control names and
// ids match the UI_MAP_ELEMENT table on console (ViewBob,
// ShowHints, ShowTooltips, InGameGamertags, ShowMashUpWorlds,
// Autosave, Difficulty, DifficultyText, Languages).
//
// This round only drives SetLabel + Init so the scene shows readable
// text instead of FJ_Label placeholders. The full parity port needs
// two more pieces we don't have yet:
//   - iOS-side game settings storage so checkbox/slider initial
//     values and write-through can actually land somewhere.
//   - Event flow from Ruffle back up when a checkbox toggles or a
//     slider moves, so we can update the backing value.
// Both are follow-ups; this at least makes the scene readable and
// navigable with B back to Settings.
- (void)initSettingsOptionsMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    // 5 checkboxes: Init(label, id, checked). Values read from the
    // persistent store (see MCLE_iOS_Settings). Mirrors
    // UIControl_CheckBox::init (3 args).
    struct Cb { const char* name; const char* label; int id; BOOL checked; };
    Cb checkboxes[] = {
        {"ViewBob",          "View Bobbing",         0, (BOOL)mcle_settings_get(MCLE_SETTING_ViewBob)},
        {"ShowHints",        "Hints",                1, (BOOL)mcle_settings_get(MCLE_SETTING_Hints)},
        {"ShowTooltips",     "In-game Tooltips",     2, (BOOL)mcle_settings_get(MCLE_SETTING_Tooltips)},
        {"InGameGamertags",  "In-game Gamertags",    3, (BOOL)mcle_settings_get(MCLE_SETTING_GamertagsVisible)},
        {"ShowMashUpWorlds", "Unhide Mashup Worlds", 4, NO},
    };
    for (auto& c : checkboxes) {
        ruffle_ios_call_init_checkbox(
            g_ruffle_player,
            (const uint8_t*)c.name,  strlen(c.name),
            (const uint8_t*)c.label, strlen(c.label),
            (double)c.id,
            c.checked ? 1 : 0);
    }

    // 2 sliders. Ranges come straight from
    // UIScene_SettingsOptionsMenu.cpp:69 (autosave 0..8) and :82
    // (difficulty 0..3). Labels match the console format strings.
    int autosave = mcle_settings_get(MCLE_SETTING_Autosave);
    int difficulty = mcle_settings_get(MCLE_SETTING_Difficulty);
    NSString* autosaveLabel = (autosave == 0)
        ? @"Autosave: Off"
        : [NSString stringWithFormat:@"Autosave: %d minutes", autosave * 15];
    static NSArray<NSString*>* kDifficultyNames = @[@"Peaceful", @"Easy", @"Normal", @"Hard"];
    int diffClamped = MAX(0, MIN(difficulty, 3));
    NSString* difficultyLabel = [NSString
        stringWithFormat:@"Difficulty: %@", kDifficultyNames[diffClamped]];

    struct Sl { const char* name; const char* label; int id; int min; int max; int current; };
    Sl sliders[] = {
        {"Autosave",   autosaveLabel.UTF8String,   5, 0, 8, autosave},
        {"Difficulty", difficultyLabel.UTF8String, 7, 0, 3, diffClamped},
    };
    for (auto& s : sliders) {
        ruffle_ios_call_init_slider(
            g_ruffle_player,
            (const uint8_t*)s.name,  strlen(s.name),
            (const uint8_t*)s.label, strlen(s.label),
            (double)s.id,
            s.min, s.max, s.current);
    }

    // 1 button: Init(label, id) via the FJ_Button path.
    const char* lang = "Languages";
    const char* langLabel = "Language Selector";
    ruffle_ios_call_init_on_named_child(
        g_ruffle_player,
        (const uint8_t*)lang, strlen(lang),
        (const uint8_t*)"Init", 4,
        (const uint8_t*)langLabel, strlen(langLabel),
        6.0);

    // Hide ShowMashUpWorlds by default: console only shows it if
    // there are hidden mashup packs to re-expose, which doesn't
    // apply on a sideloaded iOS build.
    const char* hidden = "ShowMashUpWorlds";
    ruffle_ios_call_init_on_named_child(
        g_ruffle_player,
        (const uint8_t*)hidden, strlen(hidden),
        (const uint8_t*)"HideUntilInit", 13,
        (const uint8_t*)"", 0,
        0.0);

    // Mirror console's UIScene::gainFocus first-focus path
    // (UIScene.cpp:1003-1012): call SWF SetFocus(-1) so
    // FJ_Document.SetFocus picks the authored tabIndex==1 child
    // and fires handleInitFocus via ExternalInterface.
    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    // menuButtonConfig stays unset for this scene so the DPad state
    // machine doesn't fight the SWF's internal focus handling
    // (mixed control types, not a flat button list).
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// Ports UIScene_SettingsAudioMenu (Common/UI/UIScene_SettingsAudioMenu.cpp).
// Two FJ_Slider controls, ranges 0..100, labels formatted "X: N%".
// Defaults match console's Options profile: music 100, sound 100.
// Real values need the iOS game-settings store (follow-up round).
- (void)initSettingsAudioMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    int music = mcle_settings_get(MCLE_SETTING_MusicVolume);
    int sound = mcle_settings_get(MCLE_SETTING_SoundFXVolume);
    NSArray<NSDictionary*>* sliders = @[
        @{ @"name":  @"Music",
           @"label": [NSString stringWithFormat:@"Music: %d%%", music],
           @"id":    @(0), @"min": @(0), @"max": @(100), @"cur": @(music) },
        @{ @"name":  @"Sound",
           @"label": [NSString stringWithFormat:@"Sound: %d%%", sound],
           @"id":    @(1), @"min": @(0), @"max": @(100), @"cur": @(sound) },
    ];
    for (NSDictionary* s in sliders) {
        const char* name  = [s[@"name"]  UTF8String];
        const char* label = [s[@"label"] UTF8String];
        ruffle_ios_call_init_slider(
            g_ruffle_player,
            (const uint8_t*)name,  strlen(name),
            (const uint8_t*)label, strlen(label),
            [s[@"id"]  doubleValue],
            [s[@"min"] intValue],
            [s[@"max"] intValue],
            [s[@"cur"] intValue]);
    }

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// Ports UIScene_SettingsControlMenu (Common/UI/UIScene_SettingsControlMenu.cpp).
// Two stick-sensitivity sliders named SensitivityInGame and
// SensitivityInMenu, each a FJ_Slider with range 0..200. Default
// 100 (no multiplier) until the iOS game-settings store lands.
- (void)initSettingsControlMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    int ingame = mcle_settings_get(MCLE_SETTING_SensitivityInGame);
    int inmenu = mcle_settings_get(MCLE_SETTING_SensitivityInMenu);
    NSArray<NSDictionary*>* sliders = @[
        @{ @"name":  @"SensitivityInGame",
           @"label": [NSString stringWithFormat:@"Sensitivity In-game: %d%%", ingame],
           @"id":    @(0), @"min": @(0), @"max": @(200), @"cur": @(ingame) },
        @{ @"name":  @"SensitivityInMenu",
           @"label": [NSString stringWithFormat:@"Sensitivity In-menu: %d%%", inmenu],
           @"id":    @(1), @"min": @(0), @"max": @(200), @"cur": @(inmenu) },
    ];
    for (NSDictionary* s in sliders) {
        const char* name  = [s[@"name"]  UTF8String];
        const char* label = [s[@"label"] UTF8String];
        ruffle_ios_call_init_slider(
            g_ruffle_player,
            (const uint8_t*)name,  strlen(name),
            (const uint8_t*)label, strlen(label),
            [s[@"id"]  doubleValue],
            [s[@"min"] intValue],
            [s[@"max"] intValue],
            [s[@"cur"] intValue]);
    }

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// Ports UIScene_SettingsGraphicsMenu (Common/UI/UIScene_SettingsGraphicsMenu.cpp).
// Three FJ_CheckBoxes (Clouds, BedrockFog, CustomSkinAnim) and four
// FJ_Sliders (RenderDistance, Gamma, FOV, InterfaceOpacity).
//
// Render Distance slider is 0..5 mapping to block counts {2, 4, 8,
// 16, 32, 64}. FOV slider is 0..100 mapping to 70..110 degrees via
// FOV_MIN + sliderVal*(FOV_MAX-FOV_MIN)/100. Defaults here are
// console's "medium" profile until the iOS game-settings store
// exists and we can read real values.
- (void)initSettingsGraphicsMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    struct Cb { const char* name; const char* label; int id; BOOL checked; };
    Cb checkboxes[] = {
        {"Clouds",         "Render clouds",         0, (BOOL)mcle_settings_get(MCLE_SETTING_Clouds)},
        {"BedrockFog",     "Bedrock fog",           1, (BOOL)mcle_settings_get(MCLE_SETTING_BedrockFog)},
        {"CustomSkinAnim", "Custom skin animation", 2, (BOOL)mcle_settings_get(MCLE_SETTING_CustomSkinAnim)},
    };
    for (auto& c : checkboxes) {
        ruffle_ios_call_init_checkbox(
            g_ruffle_player,
            (const uint8_t*)c.name,  strlen(c.name),
            (const uint8_t*)c.label, strlen(c.label),
            (double)c.id,
            c.checked ? 1 : 0);
    }

    static const int kDistanceTable[6] = {2, 4, 8, 16, 32, 64};
    int renderBlocks = mcle_settings_get(MCLE_SETTING_RenderDistance);
    int renderLevel = 3;
    for (int i = 0; i < 6; ++i) if (kDistanceTable[i] == renderBlocks) { renderLevel = i; break; }
    int gamma = mcle_settings_get(MCLE_SETTING_Gamma);
    int fovSlider = mcle_settings_get(MCLE_SETTING_FOV);
    int fovDeg = 70 + (fovSlider * (110 - 70)) / 100;
    int opacity = mcle_settings_get(MCLE_SETTING_InterfaceOpacity);

    NSArray<NSDictionary*>* sliders = @[
        @{ @"name":  @"RenderDistance",
           @"label": [NSString stringWithFormat:@"Render Distance: %d", renderBlocks],
           @"id":    @(3), @"min": @(0), @"max": @(5), @"cur": @(renderLevel) },
        @{ @"name":  @"Gamma",
           @"label": [NSString stringWithFormat:@"Gamma: %d%%", gamma],
           @"id":    @(4), @"min": @(0), @"max": @(100), @"cur": @(gamma) },
        @{ @"name":  @"FOV",
           @"label": [NSString stringWithFormat:@"FOV: %d", fovDeg],
           @"id":    @(5), @"min": @(0), @"max": @(100), @"cur": @(fovSlider) },
        @{ @"name":  @"InterfaceOpacity",
           @"label": [NSString stringWithFormat:@"Interface opacity: %d%%", opacity],
           @"id":    @(6), @"min": @(0), @"max": @(100), @"cur": @(opacity) },
    ];
    for (NSDictionary* s in sliders) {
        const char* name  = [s[@"name"]  UTF8String];
        const char* label = [s[@"label"] UTF8String];
        ruffle_ios_call_init_slider(
            g_ruffle_player,
            (const uint8_t*)name,  strlen(name),
            (const uint8_t*)label, strlen(label),
            [s[@"id"]  doubleValue],
            [s[@"min"] intValue],
            [s[@"max"] intValue],
            [s[@"cur"] intValue]);
    }

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// Ports UIScene_SettingsUIMenu (Common/UI/UIScene_SettingsUIMenu.cpp).
// Six FJ_CheckBoxes + two FJ_Sliders. UISize sliders have a
// non-standard range (1..3) because console uses them as direct
// labels rather than an index. Splitscreen-related controls stay
// visible for parity, but splitscreen toggling does nothing on
// iOS since there's no second-controller path.
- (void)initSettingsUIMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    struct Cb { const char* name; const char* label; int id; BOOL checked; };
    Cb checkboxes[] = {
        {"DisplayHUD",               "Display HUD",                 0, (BOOL)mcle_settings_get(MCLE_SETTING_DisplayHUD)},
        {"DisplayHand",              "Display Hand",                1, (BOOL)mcle_settings_get(MCLE_SETTING_DisplayHand)},
        {"DisplayDeathMessages",     "Display Death Messages",      2, (BOOL)mcle_settings_get(MCLE_SETTING_DeathMessages)},
        {"DisplayAnimatedCharacter", "Display Animated Character",  3, (BOOL)mcle_settings_get(MCLE_SETTING_AnimatedCharacter)},
        {"Splitscreen",              "Vertical split-screen",       4, (BOOL)mcle_settings_get(MCLE_SETTING_SplitScreenVertical)},
        {"ShowSplitscreenGamertags", "Display split-screen gamertags", 5, (BOOL)mcle_settings_get(MCLE_SETTING_DisplaySplitscreenGamertags)},
    };
    for (auto& c : checkboxes) {
        ruffle_ios_call_init_checkbox(
            g_ruffle_player,
            (const uint8_t*)c.name,  strlen(c.name),
            (const uint8_t*)c.label, strlen(c.label),
            (double)c.id,
            c.checked ? 1 : 0);
    }

    int uiSize = mcle_settings_get(MCLE_SETTING_UISize);
    int uiSizeSplit = mcle_settings_get(MCLE_SETTING_UISizeSplitscreen);
    NSArray<NSDictionary*>* sliders = @[
        @{ @"name":  @"UISize",
           @"label": [NSString stringWithFormat:@"UI Size: %d", uiSize],
           @"id":    @(6), @"min": @(1), @"max": @(3), @"cur": @(uiSize) },
        @{ @"name":  @"UISizeSplitscreen",
           @"label": [NSString stringWithFormat:@"UI Size Split-screen: %d", uiSizeSplit],
           @"id":    @(7), @"min": @(1), @"max": @(3), @"cur": @(uiSizeSplit) },
    ];
    for (NSDictionary* s in sliders) {
        const char* name  = [s[@"name"]  UTF8String];
        const char* label = [s[@"label"] UTF8String];
        ruffle_ios_call_init_slider(
            g_ruffle_player,
            (const uint8_t*)name,  strlen(name),
            (const uint8_t*)label, strlen(label),
            [s[@"id"]  doubleValue],
            [s[@"min"] intValue],
            [s[@"max"] intValue],
            [s[@"cur"] intValue]);
    }

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// Ports UIScene_LoadOrJoinMenu constructor
// (Common/UI/UIScene_LoadOrJoinMenu.cpp:536+). Two
// FJ_ButtonList_ListIconLeft controls on stage:
//   SavesList (id 0, eControl_SavesList) - user's worlds.
//   JoinList  (id 1, eControl_GamesList) - online sessions.
// The first default entry on SavesList is "Create New World"
// (JOIN_LOAD_CREATE_BUTTON_INDEX=0, AddDefaultButtons ln 1098).
// Actual save enumeration + level-generator listing is a
// separate follow-up tied to LCE save-format parsing; this
// landing just gets the widget plumbing and scene transition
// working so Play Game -> world list -> Back works end-to-end.
// JoinList stays empty on iOS (no netcode yet); the NoGames
// label is what the authored scene shows in that state.
- (void)initLoadOrJoinMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    // Init both lists with the console-side EControls enum ids.
    // matches UIScene_LoadOrJoinMenu.h:19-26.
    const char* saves = "SavesList";
    const char* games = "JoinList";
    ruffle_ios_call_list_init(g_ruffle_player,
        (const uint8_t*)saves, strlen(saves), 0.0);
    ruffle_ios_call_list_init(g_ruffle_player,
        (const uint8_t*)games, strlen(games), 1.0);

    // Seed title labels so they stop rendering their authoring-
    // time placeholder text ("FJ_Label_Black" etc.). Mirrors
    // UIScene_LoadOrJoinMenu::Initialise line 227-230:
    //   m_labelSavesListTitle.init( IDS_START_GAME );  "Start Game"
    //   m_labelJoinListTitle.init(  IDS_JOIN_GAME );   "Join Game"
    //   m_labelNoGames.init(        IDS_NO_GAMES_FOUND ); hidden initially
    // FJ_Label.SetLabel flips m_bInitialised = true itself (unlike
    // FJ_Button), so calling SetLabel without a separate Init
    // works and avoids a new label-specific FFI.
    struct LabelSeed { const char* name; const char* text; };
    LabelSeed labels[] = {
        { "SavesListTitle", "Start Game" },
        { "JoinListTitle",  "Join Game" },
        { "NoGames",        "No Games Found" },
    };
    for (auto& s : labels) {
        ruffle_ios_call_init_on_named_child(
            g_ruffle_player,
            (const uint8_t*)s.name, strlen(s.name),
            (const uint8_t*)"SetLabel", 8,
            (const uint8_t*)s.text, strlen(s.text),
            0.0);
    }

    // Console starts NoGames hidden (UIScene_LoadOrJoinMenu.cpp:230
    // setVisible(false)) and only shows it when the join list
    // actually ends up empty. We don't have the networking layer
    // that populates the join list yet, so for now we flip it on
    // to communicate "no sessions" to the user instead of leaving
    // blank space. Revisit when we have real session discovery.
    const char* noGames = "NoGames";
    ruffle_ios_set_root_child_visible(
        g_ruffle_player,
        (const uint8_t*)noGames, strlen(noGames),
        1);

    // Hide the timer controls. Console shows them (line 231-232)
    // because they're part of the auto-refresh cadence for save
    // thumbnails / session discovery; with neither wired up on
    // iOS the spinning timer art just sits there looking like a
    // bug. Revisit when the corresponding backends land.
    const char* kTimers[] = { "SavesTimer", "JoinTimer" };
    for (size_t i = 0; i < sizeof(kTimers) / sizeof(kTimers[0]); ++i) {
        const char* timer = kTimers[i];
        ruffle_ios_set_root_child_visible(
            g_ruffle_player,
            (const uint8_t*)timer, strlen(timer),
            0);
    }

    // Wipe anything still in the lists from a prior scene load
    // (shouldn't happen since replace_root_movie clears AS3
    // state, but defensive - console also clears before
    // populating in UIScene_LoadOrJoinMenu::Initialise).
    ruffle_ios_call_list_remove_all(g_ruffle_player,
        (const uint8_t*)saves, strlen(saves));
    ruffle_ios_call_list_remove_all(g_ruffle_player,
        (const uint8_t*)games, strlen(games));

    // Default first item: "Create New World". Console adds this
    // at JOIN_LOAD_CREATE_BUTTON_INDEX=0 regardless of whether
    // any saves exist (UIScene_LoadOrJoinMenu.cpp:1098).
    const char* createLabel = "Create New World";
    ruffle_ios_call_list_add_item(g_ruffle_player,
        (const uint8_t*)saves, strlen(saves),
        (const uint8_t*)createLabel, strlen(createLabel),
        0.0,
        (const uint8_t*)"", 0);

    // Auto-focus via SWF's SetFocus(-1). SavesList has tabIndex=1
    // per LoadOrJoinMenu.as __setTab_SavesList_... so FJ_Document.
    // SetFocus picks it.
    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// Ports UIScene_HowToPlayMenu constructor
// (Common/UI/UIScene_HowToPlayMenu.cpp:77-104). Single
// FJ_ButtonList_Menu named "HowToList" (eControl_Buttons = 0)
// with 24 static items from the IDS_HOW_TO_PLAY_MENU_* string
// table. Selecting an item on console calls a per-topic
// proceedToScene(eHowToPlay_*) (m_uiHTPSceneA at line 42-75) -
// those scenes are gameplay-facing tutorial pages not yet in
// scope, so handlePress here just logs for now.
- (void)initHowToPlayMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    const char* list = "HowToList";
    ruffle_ios_call_list_init(g_ruffle_player,
        (const uint8_t*)list, strlen(list), 0.0);
    ruffle_ios_call_list_remove_all(g_ruffle_player,
        (const uint8_t*)list, strlen(list));

    // Labels mirror m_uiHTPButtonNameA string-table ids in
    // UIScene_HowToPlayMenu.cpp:6-39. Non-Xbox build: SocialMedia
    // and BanningLevels are excluded. Order and ids match the
    // console enum so a later per-topic scene wire-up lines up
    // with the same press ids console's handlePress switches on.
    struct Item { const char* label; int id; };
    Item items[] = {
        { "What's New",        0 },
        { "The Basics",        1 },
        { "Multiplayer",       2 },
        { "HUD",               3 },
        { "Creative Mode",     4 },
        { "Inventory",         5 },
        { "Chests",            6 },
        { "Crafting",          7 },
        { "Furnace",           8 },
        { "Dispenser",         9 },
        { "Brewing",          10 },
        { "Enchanting",       11 },
        { "Anvil",            12 },
        { "Farming Animals",  13 },
        { "Breeding Animals", 14 },
        { "Trading",          15 },
        { "Horses",           16 },
        { "Beacons",          17 },
        { "Fireworks",        18 },
        { "Hoppers",          19 },
        { "Droppers",         20 },
        { "Nether Portal",    21 },
        { "The End",          22 },
        { "Host Options",     23 },
    };
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        ruffle_ios_call_list_add_menu_item(
            g_ruffle_player,
            (const uint8_t*)list, strlen(list),
            (const uint8_t*)items[i].label, strlen(items[i].label),
            (double)items[i].id);
    }

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

- (void)initSettingsMenuButtons {
    // Mirrors UIScene_SettingsMenu constructor on console
    // (Common/UI/UIScene_SettingsMenu.cpp). Six buttons, but the id
    // sequence skips 3 - console leaves BUTTON_ALL index 3 reserved
    // and numbers Graphics=4, UI=5, ResetToDefaults=6.
    NSArray<NSDictionary*>* cfg = @[
        @{ @"name": @"Button1", @"label": @"Options",            @"id": @(0) },
        @{ @"name": @"Button2", @"label": @"Audio",              @"id": @(1) },
        @{ @"name": @"Button3", @"label": @"Control",            @"id": @(2) },
        @{ @"name": @"Button4", @"label": @"Graphics",           @"id": @(4) },
        @{ @"name": @"Button5", @"label": @"User Interface",     @"id": @(5) },
        @{ @"name": @"Button6", @"label": @"Reset to Defaults",  @"id": @(6) },
    ];
    [self attachMenuScenery];
    [self applyMenuButtonConfig:cfg];
}

// Ports UIScene_LeaderboardsMenu constructor
// (Common/UI/UIScene_LeaderboardsMenu.cpp:120-160). The scene has
// one specialized FJ_LeaderboardList named "Gamers" plus four
// labels (Filter, Leaderboard, Entries, Info). Console populates
// the list via network reads against Xbox Live / PSN; we have no
// online service wired yet, so the list stays empty and the Info
// label carries the "offline" copy instead of an empty panel.
- (void)initLeaderboardMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    // Header labels. Console fills these from IDS_FILTER / IDS_
    // LEADERBOARD / IDS_ENTRIES / IDS_LEADERBOARDS_NOT_AVAILABLE
    // (see SetLeaderboardHeader in UIScene_LeaderboardsMenu.cpp).
    // Human-readable copy for now; swap for the real string table
    // values once the localisation pass lands.
    struct LabelSeed { const char* name; const char* text; };
    LabelSeed labels[] = {
        { "Filter",      "Filter: All Players" },
        { "Leaderboard", "Leaderboard: Kills" },
        { "Entries",     "Entries" },
        { "Info",        "Leaderboards are not available on this platform yet." },
    };
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        ruffle_ios_call_init_on_named_child(
            g_ruffle_player,
            (const uint8_t*)labels[i].name, strlen(labels[i].name),
            (const uint8_t*)"SetLabel", 8,
            (const uint8_t*)labels[i].text, strlen(labels[i].text),
            0.0);
    }

    // FJ_LeaderboardList driven from native (UIControl_LeaderboardList
    // on console uses customDraw and column feeds via handleSelection
    // ChangedRS). Our FFI doesn't cover that pathway yet, so we skip
    // initialising it. Effect: Gamers widget shows its authored
    // empty state, which is the same thing console displays before
    // the first network read completes.

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// Ports UIScene_DLCMainMenu constructor
// (Common/UI/UIScene_DLCMainMenu.cpp:25-70). One FJ_ButtonList_List
// named "OffersList" (eControl_OffersList = 0) plus OffersList_Title
// label. Console populates the list from a DLC offer query against
// the platform store (Xbox Live, PSN, etc.). Without a store
// backend we leave the list empty and seed the title + a "store
// not available" hint through the title label.
- (void)initDLCMainMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    const char* list = "OffersList";
    ruffle_ios_call_list_init(g_ruffle_player,
        (const uint8_t*)list, strlen(list), 0.0);
    ruffle_ios_call_list_remove_all(g_ruffle_player,
        (const uint8_t*)list, strlen(list));

    // FJ_ButtonList_List takes the 2-arg addNewItem shape (same as
    // FJ_ButtonList_Menu, see FJ_ButtonList_List.as:18). Seed a
    // single placeholder so the list widget has at least one
    // focusable row; pressing it just logs for now.
    const char* placeholder = "Store not available yet";
    ruffle_ios_call_list_add_menu_item(
        g_ruffle_player,
        (const uint8_t*)list, strlen(list),
        (const uint8_t*)placeholder, strlen(placeholder),
        0.0);

    const char* titleName = "OffersList_Title";
    const char* titleText = "Downloadable Content";
    ruffle_ios_call_init_on_named_child(
        g_ruffle_player,
        (const uint8_t*)titleName, strlen(titleName),
        (const uint8_t*)"SetLabel", 8,
        (const uint8_t*)titleText, strlen(titleText),
        0.0);

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// Ports UIScene_SkinSelectMenu constructor
// (Common/UI/UIScene_SkinSelectMenu.cpp). No list widget - a
// carousel of 9 UIControl_PlayerSkinPreview slots (eCharacter_
// Current + 4 on each side) wrapped in "IggyCharacters". Console
// renders per-pack skin thumbnails via custom draw and swaps the
// centre preview as you navigate. Without DLC texture loading and
// the skin registry wired, none of that renders yet. For this
// skeleton we seed the SkinNamePlate labels and the "Selected"
// pill so the scene has readable text instead of authoring-time
// "FJ_Label_Black" placeholders. Logo is already hidden by the
// scene-transition dispatcher (SkinSelectMenu1080.swf is in the
// logo-off set matching UIScene_SkinSelectMenu.cpp:143).
- (void)initSkinSelectMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    // SkinNamePlate.SkinTitle1 / SkinTitle2 are nested labels on
    // the name-plate movieclip. call_init_on_named_child walks
    // root children only, not grand-children, so this targets the
    // top-level SkinNamePlate clip and uses SetLabel there. If
    // SkinNamePlate exposes its own Init we can switch once the
    // label seeds prove the skeleton is reachable.
    const char* namePlate = "SkinNamePlate";
    const char* selectedPanel = "SelectedPanel";
    ruffle_ios_call_init_on_named_child(
        g_ruffle_player,
        (const uint8_t*)namePlate, strlen(namePlate),
        (const uint8_t*)"SetLabel", 8,
        (const uint8_t*)"Default Skin", strlen("Default Skin"),
        0.0);
    ruffle_ios_call_init_on_named_child(
        g_ruffle_player,
        (const uint8_t*)selectedPanel, strlen(selectedPanel),
        (const uint8_t*)"SetLabel", 8,
        (const uint8_t*)"Selected", strlen("Selected"),
        0.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// Present a MessageBox-style dialog overlay. Matches console's
// UIController::RequestMessageBox flow (UIController.cpp:2951):
// stash the callback / titles / buttons, navigate to the
// MessageBox SWF scene, which reads back the stash in its init
// method and configures the authored scene. Cancel (B) and button
// presses both fire the callback with the matching result code.
//
// buttonLabels is filled END-FIRST so a 2-button dialog is
// typically ["OK", "Cancel"] (OK = Button2, Cancel = Button3 on
// the authored SWF). Matches console's UIScene_MessageBox.cpp:26
// where Button0 is filled last.
- (void)presentDialogWithTitle:(NSString*)title
                       content:(NSString*)content
                       buttons:(NSArray<NSString*>*)buttonLabels
                    focusIndex:(int)focus
                      callback:(MCLEDialogCallback)callback
{
    if (!buttonLabels.count || buttonLabels.count > 4) {
        NSLog(@"[dialog] invalid button count %lu", (unsigned long)buttonLabels.count);
        if (callback) callback(MCLEDialogResultCancelled);
        return;
    }
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) {
        if (callback) callback(MCLEDialogResultCancelled);
        return;
    }

    MCLEDialogRequest* req = [MCLEDialogRequest new];
    req.title = title ?: @"";
    req.content = content ?: @"";
    req.buttonLabels = buttonLabels;
    req.focusIndex = focus;
    req.callback = callback;
    g_pending_dialog = req;

    // Dim the scene behind the dialog by flipping the
    // MenuBackground sibling on. Mirrors console's
    // addComponent(eUIComponent_MenuBackground) at UIScene_
    // MessageBox.cpp:49.
    ruffle_ios_set_xui_sibling_visible_at_depth(
        g_ruffle_player, -1, 1);
    // Hide the Minecraft logo (sibling at depth 101) while the
    // dialog is up. Without this it sits in front of the dim
    // layer and pokes out above the dialog. Restored on dismiss
    // by applyLogoVisibilityForScene: in finishDialogWithResult:.
    ruffle_ios_set_xui_sibling_visible_at_depth(
        g_ruffle_player, 101, 0);

    // Sibling-overlay model. Add MessageBox1080.swf as a stage
    // sibling at depth kMessageBoxDepth on top of the underlying
    // scene, then quiet the underlying FJ_Document with
    // clear_stage_dispatch_list + redispatch_added_to_stage so
    // ENTER/ESCAPE only trigger the dialog's nav handler. The
    // underlying scene stays alive at depth 0 the whole time so
    // it reappears instantly on dismiss without a reload.
    NSString* docsPath = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    NSString* path = [docsPath stringByAppendingPathComponent:@"MessageBox1080.swf"];
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!data.length) {
        NSLog(@"[dialog] MessageBox1080.swf not found at %@", path);
        g_pending_dialog = nil;
        ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, -1, 0);
        if (callback) callback(MCLEDialogResultCancelled);
        return;
    }
    NSString* url = [NSString stringWithFormat:@"file://%@",
                     [path stringByReplacingOccurrencesOfString:@" " withString:@"%20"]];

    // Snapshot the existing siblings' (panorama / logo / tooltips)
    // matrices around the burst so a brief 0.5s of normal animation
    // doesn't leave them with any visible drift. Only the new
    // MessageBox sibling needs frames to construct; everything else
    // can be rewound to its pre-dialog matrix on the way out.
    ruffle_ios_player_snapshot_xui_matrices(g_ruffle_player);

    int rc = ruffle_ios_add_sibling_swf_to_root(
        g_ruffle_player,
        (const uint8_t*)data.bytes, data.length,
        (const uint8_t*)url.UTF8String, strlen(url.UTF8String),
        kMessageBoxDepth, 1.0f, 1.0f, 0.0f, 0.0f);
    NSLog(@"[dialog] add_sibling MessageBox @%d -> %d", kMessageBoxDepth, rc);

    // 30-tick headless burst so the sibling's preload + ABC class
    // construct + first-frame placement finish before we drive its
    // AS3. Don't freeze siblings here: set_xui_siblings_playing(0)
    // would also freeze the just-attached MessageBox, and it needs
    // those exact frames to advance from preloaded to constructed.
    // matricesRestore at the bottom of this method wipes any drift
    // on the other siblings.
    for (int i = 0; i < 30; ++i) {
        ruffle_ios_player_tick_headless(g_ruffle_player, 1.0f / 60.0f);
    }

    [self populateMessageBoxSibling];

    ruffle_ios_player_restore_xui_matrices(g_ruffle_player);

    // Swap the bottom-strip tooltips to MessageBox's pair
    // (Select / Cancel). Mirrors UIScene_MessageBox::updateTooltips
    // (UIScene_MessageBox.cpp:74-77). Restored to the underlying
    // scene's pair in finishDialogWithResult:.
    [self seedTooltipsForScene:@"MessageBox1080.swf" hidden:NO];

    // Hand stage-level KEY_DOWN ownership over to the dialog.
    // Without this both FJ_Documents fight on every press because
    // the underlying scene's button names (Button0..N) collide
    // with MessageBox's, and FJ_Document.keyDownHandler bails by
    // name (m_this.getChildByName) which can't tell them apart.
    ruffle_ios_clear_stage_dispatch_list(g_ruffle_player);
    ruffle_ios_redispatch_added_to_stage(g_ruffle_player, kMessageBoxDepth);
}

// Drive MessageBox1080.swf's AS3 once it's attached as a sibling.
// Mirrors UIScene_MessageBox.cpp:5-55:
//   root.Init(count, focus)         hides unused buttons, wires nav
//   Title.SetLabel / Content.SetLabel
//   Button3..Button(4-count).Init(label, id)  filled end-first
//   root.AutoResize()               resize panel + shift buttons
- (void)populateMessageBoxSibling {
    extern PlayerHandle* g_ruffle_player;
    MCLEDialogRequest* req = g_pending_dialog;
    if (!g_ruffle_player || !req) return;

    int count = (int)req.buttonLabels.count;

    // root.Init(count, focusIndex).
    double initArgs[2] = { (double)count, (double)req.focusIndex };
    const char* initName = "Init";
    ruffle_ios_call_method_on_sibling_root(
        g_ruffle_player, kMessageBoxDepth,
        (const uint8_t*)initName, strlen(initName),
        initArgs, 2);

    // Title / Content labels.
    struct LabelSeed { const char* name; const char* text; };
    LabelSeed labels[] = {
        { "Title",   [req.title UTF8String] },
        { "Content", [req.content UTF8String] },
    };
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        ruffle_ios_call_init_on_sibling_child(
            g_ruffle_player, kMessageBoxDepth,
            (const uint8_t*)labels[i].name, strlen(labels[i].name),
            (const uint8_t*)"SetLabel", 8,
            (const uint8_t*)labels[i].text, strlen(labels[i].text),
            0.0);
    }

    // Fill buttons end-first. Authored slot 3 holds the LAST
    // visible button; the id is the sequence counter (0 for the
    // first visible, etc.) which is what handlePress's controlId
    // -> Accept/Decline mapping keys off (UIScene_MessageBox.cpp:
    // 120-141).
    int buttonIndex = 0;
    NSArray<NSString*>* buttonNames = @[@"Button0", @"Button1", @"Button2", @"Button3"];
    for (int slot = 4 - count; slot < 4; ++slot) {
        NSString* label = req.buttonLabels[buttonIndex];
        const char* n = [buttonNames[slot] UTF8String];
        const char* l = [label UTF8String];
        ruffle_ios_call_init_on_sibling_child(
            g_ruffle_player, kMessageBoxDepth,
            (const uint8_t*)n, strlen(n),
            (const uint8_t*)"Init", 4,
            (const uint8_t*)l, strlen(l),
            (double)buttonIndex);
        ++buttonIndex;
    }

    // root.AutoResize().
    const char* resize = "AutoResize";
    ruffle_ios_call_method_on_sibling_root(
        g_ruffle_player, kMessageBoxDepth,
        (const uint8_t*)resize, strlen(resize),
        NULL, 0);
}

// Fire the pending dialog's callback with a result and tear down
// the MessageBox sibling overlay. Shared by the handlePress route
// (for button presses) and the B-back handler (for cancellation).
- (void)finishDialogWithResult:(MCLEDialogResult)result {
    MCLEDialogRequest* req = g_pending_dialog;
    if (!req) return;  // already torn down (idempotent)
    g_pending_dialog = nil;
    extern PlayerHandle* g_ruffle_player;
    if (g_ruffle_player) {
        // Hide dim, drop the MessageBox sibling, and hand stage
        // KEY_DOWN ownership back to the underlying scene's
        // FJ_Document.
        ruffle_ios_set_xui_sibling_visible_at_depth(
            g_ruffle_player, -1, 0);
        ruffle_ios_remove_sibling_at_depth(
            g_ruffle_player, kMessageBoxDepth);
        ruffle_ios_clear_stage_dispatch_list(g_ruffle_player);
        ruffle_ios_redispatch_added_to_stage(g_ruffle_player, -1);
        // Restore the underlying scene's tooltips, logo, and focus
        // highlight. seedTooltipsForScene picks the (A, B) pair
        // that scene's updateTooltips override would have set
        // on console; applyLogoVisibilityForScene puts the logo
        // back where that scene wants it; refreshFocusState pushes
        // ChangeState=SELECTED on whichever button menuFocusIndex
        // points at.
        if (self.currentMenuSwf.length) {
            [self seedTooltipsForScene:self.currentMenuSwf hidden:NO];
            [self applyLogoVisibilityForScene:self.currentMenuSwf];
        }
        [self refreshFocusState];
    }
    if (req.callback) req.callback(result);
}

// Forward navigation: push the current menu + its focus index onto
// the back stack before swapping to the new one, so a later B-press
// returns here with the same button highlighted. Mirrors how console
// pushes the current scene state before NavigateToScene.
- (void)navigateForwardTo:(NSString*)swfName {
    if (self.currentMenuSwf.length) {
        if (!self.menuStack) self.menuStack = [NSMutableArray array];
        [self.menuStack addObject:@{
            @"swf":   self.currentMenuSwf,
            @"focus": @(self.menuFocusIndex),
        }];
    }
    [self transitionToMenuNamed:swfName];
}

// Back navigation: pop the stack and restore the previous scene
// plus its focus index. Mirrors UIController::NavigateBack
// (Common/UI/UIController.cpp). Empty stack falls back to MainMenu
// focus 0 so the user never gets stranded.
- (void)navigateBack {
    NSString* target = nil;
    int restoreFocus = 0;
    if (self.menuStack.count > 0) {
        NSDictionary* entry = self.menuStack.lastObject;
        target = entry[@"swf"];
        restoreFocus = [entry[@"focus"] intValue];
        [self.menuStack removeLastObject];
    }
    if (!target.length) target = @"MainMenu1080.swf";
    [self transitionToMenuNamed:target];
    // transitionToMenuNamed resets menuFocusIndex to 0 inside its
    // init-buttons callback. Override after it runs so the focused
    // button is the one the user left on.
    [self setMenuFocusIndex:restoreFocus];
    [self refreshFocusState];
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
    // Clear AS3 stage focus so the previous scene's focus highlight
    // doesn't paint on top of the new scene. Settings scenes call
    // SWF SetFocus(-1) via ruffle_ios_call_root_set_focus inside
    // their init methods to re-establish a scene-local focus,
    // matching console's UIScene::gainFocus path.
    ruffle_ios_clear_focus(g_ruffle_player);

    // Snapshot panorama/logo/tooltips matrices before the whole
    // transition sequence. A per-tick restore inside the 30-tick
    // headless burst wasn't enough: the ~15 call_init_on_named_child
    // calls that follow each advance the executor by a few px, adding
    // up to ~45 authored px of scroll (the visible jump). Snapshot
    // once here, restore once at the end, and everything in between
    // (replace_swf, 30 ticks, applyMenuButtonConfig) can drift freely
    // without affecting the final panorama position.
    ruffle_ios_player_snapshot_xui_matrices(g_ruffle_player);

    int rc = ruffle_ios_player_replace_swf(
        g_ruffle_player,
        (const uint8_t*)data.bytes, data.length,
        (const uint8_t*)url.UTF8String, strlen(url.UTF8String));
    NSLog(@"[MinecraftVC] transition -> %@ rc=%d", swfName, rc);
    if (rc == 1) {
        self.currentMenuSwf = swfName;
        g_current_scene_name = swfName ?: @"";
        self.menuFocusIndex = 0;
        // Re-roll the splash when entering MainMenu. Console does
        // the same every time UIScene_MainMenu is constructed.
        if ([swfName isEqualToString:@"MainMenu1080.swf"]) {
            [self pickSplash];
        }
        // Advance the scene HEADLESS (no render) so its async imports
        // finish and Button1..ButtonN get placed, but the surface still
        // shows the previous frame. This matches console's scene-swap
        // semantics: initialiseMovie + control init happen before the
        // scene becomes visible. Without this, the new menu renders
        // with Flash authoring-time placeholder text ("FJ_Label") for
        // the ~500ms it takes our Init calls to land.
        // Freeze every non-root stage child's Timeline (panorama,
        // logo, tooltips) across the burst so their scroll animations
        // don't drift while the new scene constructs. Root scene
        // still advances at full dt so Init calls can land and
        // labels resolve. Play resumes immediately after the loop.
        ruffle_ios_player_set_xui_siblings_playing(g_ruffle_player, 0);
        for (int i = 0; i < 30; ++i) {
            ruffle_ios_player_tick_headless(g_ruffle_player, 1.0f / 60.0f);
        }
        ruffle_ios_player_set_xui_siblings_playing(g_ruffle_player, 1);
        if ([swfName isEqualToString:@"MainMenu1080.swf"]) {
            [self initMainMenuButtons];
        } else if ([swfName isEqualToString:@"HelpAndOptionsMenu1080.swf"]) {
            [self initHelpAndOptionsButtons];
        } else if ([swfName isEqualToString:@"SettingsMenu1080.swf"]) {
            [self initSettingsMenuButtons];
        } else if ([swfName isEqualToString:@"SettingsOptionsMenu1080.swf"]) {
            [self initSettingsOptionsMenu];
        } else if ([swfName isEqualToString:@"SettingsAudioMenu1080.swf"]) {
            [self initSettingsAudioMenu];
        } else if ([swfName isEqualToString:@"SettingsControlMenu1080.swf"]) {
            [self initSettingsControlMenu];
        } else if ([swfName isEqualToString:@"SettingsGraphicsMenu1080.swf"]) {
            [self initSettingsGraphicsMenu];
        } else if ([swfName isEqualToString:@"SettingsUIMenu1080.swf"]) {
            [self initSettingsUIMenu];
        } else if ([swfName isEqualToString:@"LoadOrJoinMenu1080.swf"]) {
            [self initLoadOrJoinMenu];
        } else if ([swfName isEqualToString:@"HowToPlayMenu1080.swf"]) {
            [self initHowToPlayMenu];
        } else if ([swfName isEqualToString:@"LeaderboardMenu1080.swf"]) {
            [self initLeaderboardMenu];
        } else if ([swfName isEqualToString:@"DLCMainMenu1080.swf"]) {
            [self initDLCMainMenu];
        } else if ([swfName isEqualToString:@"SkinSelectMenu1080.swf"]) {
            [self initSkinSelectMenu];
        }

        // Per-scene scenery visibility. Console drives this per
        // scene via updateComponents() calling
        // UILayer::showComponent(eUIComponent_Logo / _Panorama, bool)
        // (UILayer.cpp:544+). A table here mirrors the source-side
        // decisions. Scenes not listed keep the default (Logo on,
        // Panorama on) so MainMenu / HelpAndOptions / LoadOrJoin /
        // HowToPlay / Settings all look right without per-scene
        // changes. The logo-off list matches which scenes pass
        // false to showComponent for eUIComponent_Logo in their
        // updateComponents() implementations:
        //   SkinSelectMenu.cpp:143
        //   LeaderboardsMenu.cpp:150
        //   CreateWorldMenu.cpp:263
        //   LaunchMoreOptionsMenu.cpp:201
        // DLCMainMenu also wants logo off once wired (its panel
        // covers the full upper half of the screen).
        [self applyLogoVisibilityForScene:swfName];

        // Seed the Tooltips bottom-strip on every scene transition.
        // Mirrors what each console UIScene::updateTooltips does
        // (e.g. UIScene_SettingsAudioMenu.cpp:45 calls
        // ui.SetTooltips(iPad, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK)
        // which routes to UIController::SetTooltips and then to
        // UIComponent_Tooltips::SetTooltips, eventually firing
        // ToolTips.as SetToolTip per button).
        // seedTooltipsForScene picks the right (A, B) pair per
        // scene to match each console UIScene::updateTooltips
        // override. MessageBox stays visible with Select / Cancel
        // instead of being hidden, matching console.
        [self seedTooltipsForScene:swfName hidden:NO];
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
    // Restore the panorama/logo/tooltips matrices we stashed at the
    // start of this method. Anything that advanced the XUI bitmap
    // tx during the transition gets wiped; normal scroll resumes
    // from where it was visible before the A-press.
    ruffle_ios_player_restore_xui_matrices(g_ruffle_player);
}

- (void)tick:(CADisplayLink*)link {
    mcle_game_tick();

    // Splash text pulse + position. Mirrors CXuiCtrlSplashPulser::
    // OnRender (Common/XUI/XUI_Ctrl_SplashPulser.cpp): pulses with
    // sin(currentTimeMillis), rotated -17 degrees, centered at the
    // authored (612, 126) position on the 1920x1080 stage. Only
    // visible on MainMenu; hidden on sub-scenes to match console.
    if (self.splashLabel) {
        BOOL onMainMenu = [self.currentMenuSwf isEqualToString:@"MainMenu1080.swf"];
        // Hide while a dialog is up too: the splash is a UIKit
        // UILabel painted on top of the wgpu surface, so it would
        // sit in front of the dialog otherwise.
        self.splashLabel.hidden = !onMainMenu || (g_pending_dialog != nil);
        if (onMainMenu && !g_pending_dialog) {
            uint64_t nowMs = (uint64_t)(CACurrentMediaTime() * 1000.0);
            double phase = (double)(nowMs % 1000) / 1000.0 * M_PI * 2.0;
            double pulse = 1.0 - fabs(sin(phase)) * 0.05;
            // Stage fits 1920x1080 inside view by height, centered
            // horizontally. Compute in UIKit points from view bounds
            // so this follows screen rotation, safe area, anything.
            CGSize vb = self.view.bounds.size;
            CGFloat stageH = vb.height;
            CGFloat stageW = stageH * (1920.0 / 1080.0);
            CGFloat stageX = (vb.width - stageW) * 0.5;
            CGFloat authoredScale = stageH / 1080.0;
            // Authored XUI position is (612, 126) top-left with size
            // (500, 50) on a 1280x720 stage. MainMenu1080.swf is the
            // 1.5x-scaled version generated for 1920x1080, so on our
            // stage the center lands at
            //   (612*1.5 + 250*1.5, 126*1.5 + 25*1.5)
            // = (918 + 375, 189 + 37.5) = (1293, 226.5).
            CGFloat sx = stageX + 1293.0 * authoredScale;
            CGFloat sy = 226.5 * authoredScale;
            CGFloat rot = -17.0 * M_PI / 180.0;
            self.splashLabel.transform = CGAffineTransformIdentity;
            [self.splashLabel sizeToFit];
            self.splashLabel.center = CGPointMake(sx, sy);
            self.splashLabel.transform = CGAffineTransformScale(
                CGAffineTransformMakeRotation(rot),
                (CGFloat)pulse, (CGFloat)pulse);
        }
    }

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
            // When a dialog overlay is up, this whole block is
            // skipped: the underlying scene is silent (stage
            // KEY_DOWN was redispatched onto the MessageBox
            // sibling so its FJ_Document owns input) and the iOS
            // state machine must not animate or navigate behind
            // the dialog. B routes through
            // finishDialogWithResult: for cancellation, matching
            // UIScene_MessageBox::handleInput (line 99-105).
            NSArray<NSDictionary*>* cfg = g_pending_dialog ? nil : self.menuButtonConfig;
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
                    // Console UIController::PlayUISFX pitches Focus
                    // by +-0.05 randomly so repeat navigation doesn't
                    // fatigue. Matches Common/UI/UIController.cpp:2533.
                    float jitter = ((float)arc4random_uniform(1000) / 1000.0f - 0.5f) * 0.1f;
                    mcle_audio_play_ui_sfx("focus", 1.0f, 1.0f + jitter);
                }
                if (pressedNow & 0x00000001u) {  // A -> PRESSED
                    changeState(cur, 3);
                    mcle_audio_play_ui_sfx("press", 1.0f, 1.0f);
                    int id = [cfg[cur][@"id"] intValue];
                    NSLog(@"[MinecraftVC] menu press -> %@ (id=%d)",
                          cfg[cur][@"name"], id);
                    // Scene transitions per current menu + pressed id.
                    if ([self.currentMenuSwf isEqualToString:@"MainMenu1080.swf"]) {
                        // Mirrors UIScene_MainMenu::handlePress
                        // (Common/UI/UIScene_MainMenu.cpp:301).
                        // PlayGame opens a sign-in flow then
                        // CreateLoad/LoadOrJoin; we skip sign-in and
                        // go straight to the load/join picker.
                        // Achievements on console opens the Xbox
                        // Live platform overlay, not a game SWF, so
                        // there's nothing to transition to on iOS.
                        if (id == 0) {
                            [self navigateForwardTo:@"LoadOrJoinMenu1080.swf"];
                        } else if (id == 1) {
                            [self navigateForwardTo:@"LeaderboardMenu1080.swf"];
                        } else if (id == 2) {
                            NSLog(@"[MinecraftVC] Achievements pressed (no LCE scene on this platform)");
                        } else if (id == 3) {
                            [self navigateForwardTo:@"HelpAndOptionsMenu1080.swf"];
                        } else if (id == 4) {
                            [self navigateForwardTo:@"DLCMainMenu1080.swf"];
                        } else if (id == 5) {
                            // Exit Game. Console opens a confirm
                            // dialog before quitting; IUIScene_
                            // PauseMenu::ExitGameDialogReturned
                            // (IUIScene_PauseMenu.cpp:19) runs the
                            // actual shutdown only on Accept. Match
                            // that path here through the
                            // presentDialog helper. iOS self-
                            // terminate is normally an App Store
                            // reject but this is a sideloaded dev
                            // build so exit(0) is fine. Small delay
                            // after the callback so the SFX plays
                            // before the process dies.
                            [self presentDialogWithTitle:@"Exit Game"
                                                 content:@"Are you sure you want to exit?"
                                                 buttons:@[@"OK", @"Cancel"]
                                              focusIndex:1
                                                callback:^(MCLEDialogResult r) {
                                if (r != MCLEDialogResultAccept) return;
                                dispatch_after(
                                    dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)),
                                    dispatch_get_main_queue(), ^{
                                        NSLog(@"[MinecraftVC] Exit Game confirmed, terminating");
                                        mcle_audio_stop_menu_music();
                                        exit(0);
                                    });
                            }];
                        }
                    } else if ([self.currentMenuSwf isEqualToString:
                                @"HelpAndOptionsMenu1080.swf"]) {
                        // Mirrors UIScene_HelpAndOptionsMenu::handlePress
                        // (Common/UI/UIScene_HelpAndOptionsMenu.cpp:208).
                        // BUTTON_HAO_CHANGESKIN=0, HOWTOPLAY=1, CONTROLS=2,
                        // SETTINGS=3, CREDITS=4. Each navigates to its own
                        // UIScene whose getMoviePath returns the SWF
                        // basename below (non-splitscreen, single-player
                        // path, matching single-player iOS case).
                        NSString* target = nil;
                        switch (id) {
                            case 0: target = @"SkinSelectMenu1080.swf"; break;
                            case 1: target = @"HowToPlayMenu1080.swf"; break;
                            case 2: target = @"Controls1080.swf"; break;
                            case 3: target = @"SettingsMenu1080.swf"; break;
                            case 4: target = @"Credits1080.swf"; break;
                        }
                        if (target) [self navigateForwardTo:target];
                    } else if ([self.currentMenuSwf isEqualToString:
                                @"SettingsMenu1080.swf"]) {
                        // Mirrors UIScene_SettingsMenu::handlePress
                        // (Common/UI/UIScene_SettingsMenu.cpp:117). ids
                        // jump from 2 to 4 on console because BUTTON_ALL
                        // slot 3 is reserved. Reset to Defaults on id 6
                        // opens a confirm dialog on console and calls
                        // CMinecraftApp::SetDefaultOptions on OK
                        // (Consoles_App.cpp:857+ sets MusicVolume=100,
                        // SoundFXVolume=100, RenderDistance=16, Gamma=
                        // 50, Difficulty=1, Autosave=2, Clouds=1, ...
                        // across ~30 settings). We skip the confirm
                        // dialog for now and run the reset immediately.
                        NSString* target = nil;
                        switch (id) {
                            case 0: target = @"SettingsOptionsMenu1080.swf"; break;
                            case 1: target = @"SettingsAudioMenu1080.swf"; break;
                            case 2: target = @"SettingsControlMenu1080.swf"; break;
                            case 4: target = @"SettingsGraphicsMenu1080.swf"; break;
                            case 5: target = @"SettingsUIMenu1080.swf"; break;
                            case 6:
                                // Console opens a confirm dialog
                                // ("Reset to default settings?") before
                                // applying. We drive that same path
                                // through presentDialogWithTitle:...:
                                // and only run the reset on Accept.
                                [self presentDialogWithTitle:@"Reset to Defaults"
                                                     content:@"This will reset every setting to its default value. Continue?"
                                                     buttons:@[@"OK", @"Cancel"]
                                                  focusIndex:1
                                                    callback:^(MCLEDialogResult r) {
                                    if (r == MCLEDialogResultAccept) {
                                        mcle_settings_reset_to_defaults();
                                        // Audio is the one setting
                                        // that's live-applied, so
                                        // push it through immediately.
                                        mcle_audio_set_music_volume(
                                            mcle_settings_get(MCLE_SETTING_MusicVolume));
                                        mcle_audio_set_sfx_volume(
                                            mcle_settings_get(MCLE_SETTING_SoundFXVolume));
                                    }
                                }];
                                break;
                        }
                        if (target) [self navigateForwardTo:target];
                    }
                }
            }
            // B button (code=1, mask 0x00000002). Priority order:
            //   1. Dialog up -> Cancelled (regardless of which
            //      underlying scene is showing). Matches console
            //      UIScene_MessageBox::handleInput line 99-105.
            //   2. Otherwise on a non-MainMenu scene -> back.
            //   3. MainMenu B is a no-op (no scene to pop to).
            if (pressedNow & 0x00000002u) {
                if (g_pending_dialog) {
                    mcle_audio_play_ui_sfx("back", 1.0f, 1.0f);
                    [self finishDialogWithResult:MCLEDialogResultCancelled];
                } else if (![self.currentMenuSwf isEqualToString:@"MainMenu1080.swf"]) {
                    NSLog(@"[MinecraftVC] back from %@ (stack depth %lu)",
                          self.currentMenuSwf,
                          (unsigned long)self.menuStack.count);
                    mcle_audio_play_ui_sfx("back", 1.0f, 1.0f);
                    [self navigateBack];
                }
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
