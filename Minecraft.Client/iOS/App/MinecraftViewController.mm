#import "MinecraftViewController.h"
#import "MetalView.h"

#import <QuartzCore/CADisplayLink.h>
#import <GameController/GameController.h>
#import <CoreText/CoreText.h>

#include "RND_iOS_Stub.h"
#include "INP_iOS_Bridge.h"
#include "mcle_swf_bridge.h"
#include "ruffle_ios.h"
#include "MCLE_iOS_Audio.h"
#include "MCLE_iOS_Settings.h"
#include "../Game/MCLESaveList.h"
#include "../Game/MCLEStrings.h"
#include "../Game/MCLEHud.h"
#include "../Game/MCLETooltips.h"
#include "../Game/MCLEContainerMenu.h"
#include "../Game/MCLEKeyboard.h"
#include "../Game/MCLESignEntryMenu.h"
#include "../Game/MCLEInGameInfoMenu.h"
#include "../Game/MCLETutorialPopup.h"
#include "../Game/MCLESkinSelectMenu.h"
#include "../Game/MCLEDLC.h"
#include "../Game/MCLEStartGame.h"
#include "../Game/MCLETexturePacks.h"
#include "../../../upstream/Minecraft.Client/Windows64Media/strings.h"
#include "../../../upstream/Minecraft.Client/Windows64/Xbox_BuildVer.h"

#include <algorithm>
#include <atomic>
#include <vector>

extern "C" void mcle_game_tick(void);  // GameBootstrap.cpp
extern "C" int  mcle_game_start(const char *levelFolder);   // MCLEGameLoop.cpp
extern "C" int  mcle_game_load_status(int *percent);        // MCLEGameLoop.cpp
extern "C" int  mcle_game_load_is_new(void);                // MCLEGameLoop.cpp
extern "C" int  mcle_game_load_finding_seed(void);          // MCLEGameLoop.cpp
extern "C" void mcle_minecraft_main(void);                  // MCLEGameLoop.cpp
extern "C" int  mcle_level_generators_refresh(void);        // MCLEGameLoop.cpp
extern "C" const char* mcle_level_generator_name(int i);    // MCLEGameLoop.cpp
extern "C" int  mcle_game_start_level_gen(int i);           // MCLEGameLoop.cpp

// app.GetString(IDS_*)
static NSString* MCLEStr(int ids) {
    const char* s = mcle_string(ids);
    return (s && *s) ? [NSString stringWithUTF8String:s] : @"";
}

// CMinecraftApp::InitialiseTips + GetNextTip. Skin pack tip first, then
// the game tips (first one fixed, rest shuffled) with a trivia tip every
// third slot.
static int MCLENextTip(void) {
    static std::vector<int> order;
    static size_t next = 0;
    static bool shownSkinTip = false;
    if (!shownSkinTip) {
        shownSkinTip = true;
        return IDS_TIPS_GAMETIP_SKINPACKS;
    }
    if (order.empty()) {
        std::vector<int> game = { IDS_TIPS_GAMETIP_1, IDS_TIPS_GAMETIP_2, IDS_TIPS_GAMETIP_3, IDS_TIPS_GAMETIP_4, IDS_TIPS_GAMETIP_5, IDS_TIPS_GAMETIP_6, IDS_TIPS_GAMETIP_7, IDS_TIPS_GAMETIP_8, IDS_TIPS_GAMETIP_9, IDS_TIPS_GAMETIP_10, IDS_TIPS_GAMETIP_11, IDS_TIPS_GAMETIP_12, IDS_TIPS_GAMETIP_13, IDS_TIPS_GAMETIP_14, IDS_TIPS_GAMETIP_15, IDS_TIPS_GAMETIP_16, IDS_TIPS_GAMETIP_17, IDS_TIPS_GAMETIP_18, IDS_TIPS_GAMETIP_19, IDS_TIPS_GAMETIP_20, IDS_TIPS_GAMETIP_21, IDS_TIPS_GAMETIP_22, IDS_TIPS_GAMETIP_23, IDS_TIPS_GAMETIP_24, IDS_TIPS_GAMETIP_25, IDS_TIPS_GAMETIP_26, IDS_TIPS_GAMETIP_27, IDS_TIPS_GAMETIP_28, IDS_TIPS_GAMETIP_29, IDS_TIPS_GAMETIP_30, IDS_TIPS_GAMETIP_31, IDS_TIPS_GAMETIP_32, IDS_TIPS_GAMETIP_33, IDS_TIPS_GAMETIP_34, IDS_TIPS_GAMETIP_35, IDS_TIPS_GAMETIP_36, IDS_TIPS_GAMETIP_37, IDS_TIPS_GAMETIP_38, IDS_TIPS_GAMETIP_39, IDS_TIPS_GAMETIP_40, IDS_TIPS_GAMETIP_41, IDS_TIPS_GAMETIP_42, IDS_TIPS_GAMETIP_43, IDS_TIPS_GAMETIP_44, IDS_TIPS_GAMETIP_45, IDS_TIPS_GAMETIP_46, IDS_TIPS_GAMETIP_47, IDS_TIPS_GAMETIP_48, IDS_TIPS_GAMETIP_49, IDS_TIPS_GAMETIP_50 };
        std::vector<int> trivia = { IDS_TIPS_TRIVIA_1, IDS_TIPS_TRIVIA_2, IDS_TIPS_TRIVIA_3, IDS_TIPS_TRIVIA_4, IDS_TIPS_TRIVIA_5, IDS_TIPS_TRIVIA_6, IDS_TIPS_TRIVIA_7, IDS_TIPS_TRIVIA_8, IDS_TIPS_TRIVIA_9, IDS_TIPS_TRIVIA_10, IDS_TIPS_TRIVIA_11, IDS_TIPS_TRIVIA_12, IDS_TIPS_TRIVIA_13, IDS_TIPS_TRIVIA_14, IDS_TIPS_TRIVIA_15, IDS_TIPS_TRIVIA_16, IDS_TIPS_TRIVIA_17, IDS_TIPS_TRIVIA_18, IDS_TIPS_TRIVIA_19, IDS_TIPS_TRIVIA_20 };
        for (size_t i = game.size() - 1; i > 1; --i) std::swap(game[i], game[1 + arc4random_uniform((uint32_t)i)]);
        for (size_t i = trivia.size() - 1; i > 0; --i) std::swap(trivia[i], trivia[arc4random_uniform((uint32_t)(i + 1))]);
        size_t g = 0, t = 0;
        for (size_t i = 0; i < game.size() + trivia.size(); ++i) {
            if (i % 3 == 2 && t < trivia.size()) order.push_back(trivia[t++]);
            else if (g < game.size()) order.push_back(game[g++]);
            else order.push_back(trivia[t++]);
        }
    }
    if (next >= order.size()) next = 0;
    return order[next++];
}

// UIScene_LoadMenu state the bridge and the loading screen need.
static int  g_load_save_index = -1;
// UIScene_LoadOrJoinMenu::m_iDefaultButtonsC: Create New World and the level
// generators come before the saves
static int  g_load_default_buttons = 1;
// UIScene_LoadOrJoinMenu's m_iSaveListIndex and m_bIgnoreInput, and the
// control UIScene has the focus on (m_iFocusControl)
static int  g_load_save_list_index = 0;
static int  g_load_focus_control = -1;
static bool g_load_or_join_ignore_input = false;
static int  g_load_game_mode = 0;              // GameType id, 0 survival 1 creative
static bool g_load_has_been_in_creative = false;
static bool g_load_is_save_owner = true;       // m_bIsSaveOwner
// Opened from the save list, not come back to from More Options
static bool g_load_menu_fresh = false;
static bool g_loading_world = false;
// IUIScene_StartGame::m_MoreOptionsParams of the LoadMenu or CreateWorldMenu
// that's up. LaunchMoreOptionsMenu changes it.
static MCLELaunchMoreOptions g_start_options;
// UIScene_CreateWorldMenu. It stays alive under LaunchMoreOptionsMenu.
static NSString* g_create_world_name = @"";   // m_worldName
static int  g_create_game_mode_id = 0;         // m_iGameModeId
static bool g_create_game_mode_creative = false;
static bool g_create_ignore_input = false;
// IUIScene_StartGame's texture pack list on the LoadMenu or CreateWorldMenu:
// m_currentTexturePackIndex, m_texturePackDescDisplayed, and m_bIgnoreInput
// while the DLC packs it lists are still being mounted
static int  g_texture_pack_index = 0;
static bool g_texture_pack_desc_displayed = false;
static bool g_texture_pack_waiting_for_dlc = false;
// UIScene_LaunchMoreOptionsMenu: m_tabIndex, 0 world options, 1 game options
static int  g_more_options_tab = 0;
static bool g_more_options_ignore_input = false;
static void MCLEMoreOptionsTooltips(void);
// UIScene_PauseMenu is up and the single player game is paused under it
static bool g_world_paused = false;
// UIScene_DeathMenu is up
static bool g_world_dead = false;
// A container menu (UIScene_InventoryMenu) is up over the running world
static bool g_world_container = false;
// A container scene's movie is going back up for its reloadMovie, after the
// scene that was over it went
static bool g_container_reloading = false;
// The same for a front end menu, after the keyboard over it went
static bool g_menu_reloading = false;
// The container scenes up are UIScene_InGameInfoMenu's, which isn't a
// container menu or one that holds off the autosave
static bool g_world_info = false;
static bool MCLELabelAt(const char* path, std::wstring* label);
// Exit Game: FullscreenProgress is up while the world is torn down
static bool g_exiting_world = false;
// UIScene_ConnectingProgress is up while the player changes level, until the
// change it waits for is done (and the respawn, after the End poem)
static bool g_world_connecting = false;
static bool g_connecting_respawn = false;
static int g_level_changes_seen = 0;
static int g_respawns_seen = 0;
// UIScene_EndPoem is up after the End's exit portal
static bool g_world_won = false;
static int g_wins_seen = 0;
// UIScene_FullscreenProgress is up while the server saves, for the pause
// menu's Save or an autosave, over the game (0), the pause menu (1) or the
// in game info menu (2)
static bool g_world_saving = false;
static bool g_saving_autosave = false;
static int g_saving_over = 0;
// UIScene_EndPoem's m_requestedLabel and m_bIgnoreInput
static std::atomic<int> g_endpoem_requested{-1};
static bool g_endpoem_ignore_input = false;
extern "C" int  mcle_game_level_changes(int *finished, int *stringId); // MCLEGameLoop.cpp
extern "C" int  mcle_game_wins(void);                // MCLEGameLoop.cpp
extern "C" int  mcle_game_respawns(void);            // MCLEGameLoop.cpp
extern "C" void mcle_end_poem_init(void);            // MCLEEndPoem.cpp
extern "C" int  mcle_end_poem_paragraph_count(void); // MCLEEndPoem.cpp
extern "C" const char *mcle_end_poem_paragraph(int index); // MCLEEndPoem.cpp
extern "C" void mcle_end_poem_input(unsigned int buttons, unsigned int prevButtons,
                                    void (*sendKey)(int iggyKeyCode, int down)); // MCLEEndPoem.cpp
extern "C" int mcle_game_exit_world(int save);
extern "C" int mcle_game_exit_world_done(void);

static void MCLESelectSaveSize(int saveIndex);
static void MCLELoadOrJoinFocusChange(int controlId, int childId);
static void MCLELoadOrJoinTooltips(void);
extern "C" int  mcle_world_is_ticking(void);  // MCLEGameLoop.cpp
extern "C" void mcle_render_frame(void);      // RND_iOS_Metal.mm
extern "C" void mcle_metal_add_phase_ms(int phase, double ms);
extern "C" void mcle_metal_set_gamma(float gamma); // MetalContext.mm
extern "C" void mcle_metal_attach_ui_layer(void* ca_metal_layer, int width, int height); // MetalContext.mm
extern "C" void mcle_game_set_paused(int paused);   // MCLEGameLoop.cpp
extern "C" int  mcle_player_is_dead(void);           // MCLEGameLoop.cpp
extern "C" void mcle_game_respawn(void);             // MCLEGameLoop.cpp
extern "C" int  mcle_world_game_time(long long *gameTime, int *dimension); // MCLEGameLoop.cpp
extern "C" int  mcle_game_save(int autosave);        // MCLEGameLoop.cpp
extern "C" int  mcle_game_save_done(void);           // MCLEGameLoop.cpp
extern "C" void mcle_game_progress(int reset, int *title, int *status, int *percent); // MCLEGameLoop.cpp
extern "C" void mcle_storage_set_save_folder(const char *folder); // MCLESaveList.cpp
extern "C" void mcle_capture_save_thumbnail(void);   // MCLESaveList.cpp
extern "C" void mcle_game_host_settings_changed(void); // MCLEGameLoop.cpp
void mcle_sound_update_music_volume(float v);          // MCLESound.cpp
void mcle_sound_update_sfx_volume(float v);
extern "C" int  mcle_metal_thumbnail_pending(void);  // MetalContext.mm

// The eAppAction_*CapturedThumbnail step: Minecraft::run takes the save's
// thumbnail from the next world frame, then the save or the exit goes ahead
static void (^g_after_thumbnail)(void) = nil;
static int g_after_thumbnail_frames = 0;
static void MCLECaptureThumbnailThen(void (^then)(void)) {
    void (^before)(void) = g_after_thumbnail;
    g_after_thumbnail = before ? [^{ before(); then(); } copy] : [then copy];
    g_after_thumbnail_frames = 0;
    mcle_capture_save_thumbnail();
}

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

// Stage-sibling depth assignments. Stack is back-to-front:
//   -2  Panorama
//    0  root scene (the active menu SWF)
//  101  Logo (ComponentLogo1080.swf)
//  199  MenuBackground (the dim overlay, hidden by default,
//       shown while a dialog is up). Sits ABOVE the logo so
//       the dim covers it, matching console where MessageBox's
//       addComponent(eUIComponent_MenuBackground) darkens
//       everything underneath including the logo.
//  200  MessageBox dialog (sibling, only when dialog up)
//  201  Tooltips bottom strip - lives above the dialog so
//       Select / Cancel stay readable while the dim is up.
//       The strip is at the bottom of the screen so it doesn't
//       visually overlap the dialog panel.
static const int kLogoDepth        = 101;
static const int kDimDepth         = 199;
// The in-game menus' MenuBackground, under the scene like UILayer draws
// its components before its scenes. 199 is the MessageBox's own one.
static const int kMenuBackgroundDepth = -1;
// UIScene::getSafeZoneHalfHeight / HalfWidth on a HiDef screen: 5% of the
// 1920x1080 the movies are laid out for
static const int kSafeZoneTop    = 1080 / 20;
static const int kSafeZoneBottom = 1080 / 20;
static const int kSafeZoneLeft   = 1920 / 20;
static const int kSafeZoneRight  = 1920 / 20;
static const int kMessageBoxDepth  = 200;
static const int kTooltipsDepth    = 201;
// UIComponent_TutorialPopup, on the popup layer over the scenes (the HUD
// and the container menus)
static const int kTutorialPopupDepth = 150;
// UIComponent_PressStartToPlay, on the fullscreen group's tooltips layer
// over everything else
static const int kPressStartToPlayDepth = 202;

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
- (void)navigateForwardTo:(NSString*)swfName;
- (void)loadMenuPress:(int)controlId child:(int)childId;
- (void)loadMenuSlider:(int)sliderId value:(int)value;
- (void)createWorldPress:(int)controlId child:(int)childId;
- (void)createWorldSlider:(int)sliderId value:(int)value;
- (void)startGameSelectionChanged:(int)selectedId;
- (void)moreOptionsPress:(int)controlId;
- (void)moreOptionsSlider:(int)sliderId value:(int)value;
- (void)moreOptionsCheckbox:(int)controlId checked:(BOOL)selected;
- (void)moreOptionsFocus:(int)controlId;
- (void)introNavigate;
@end
static __weak MinecraftViewController* g_active_vc = nil;

// UIScene_Intro::m_bIgnoreNavigate
static bool g_intro_ignore_navigate = false;

static int MCLECallPath(NSString* path, NSString* method, NSArray* args);
static void MCLETutorialPopupApplyOps(void);
void mcle_game_set_menu_displayed(bool displayed); // Game/MCLEGameLoop.cpp
bool mcle_game_menu_displayed();                   // Game/MCLEGameLoop.cpp
extern "C" void mcle_game_check_menu_displayed(void); // Game/MCLEGameLoop.cpp

// What each UIScene_Settings*Menu labels a slider with, both in init and
// in handleSliderMove. value is the slider's own value.
static NSString* MCLESettingsSliderLabel(NSString* scene, int sliderId, int value) {
    if ([scene isEqualToString:@"SettingsAudioMenu1080.swf"]) {
        if (sliderId == 0) return [NSString stringWithFormat:@"%@: %d%%", MCLEStr(IDS_SLIDER_MUSIC), value];
        if (sliderId == 1) return [NSString stringWithFormat:@"%@: %d%%", MCLEStr(IDS_SLIDER_SOUND), value];
    } else if ([scene isEqualToString:@"SettingsControlMenu1080.swf"]) {
        if (sliderId == 0) return [NSString stringWithFormat:@"%@: %d%%", MCLEStr(IDS_SLIDER_SENSITIVITY_INGAME), value];
        if (sliderId == 1) return [NSString stringWithFormat:@"%@: %d%%", MCLEStr(IDS_SLIDER_SENSITIVITY_INMENU), value];
    } else if ([scene isEqualToString:@"SettingsOptionsMenu1080.swf"]) {
        if (sliderId == 5) {
            if (value == 0) return MCLEStr(IDS_SLIDER_AUTOSAVE_OFF);
            return [NSString stringWithFormat:@"%@: %d %@", MCLEStr(IDS_SLIDER_AUTOSAVE), value * 15, MCLEStr(IDS_MINUTES)];
        }
        if (sliderId == 7) {
            static const int kTitles[4] = { IDS_DIFFICULTY_TITLE_PEACEFUL, IDS_DIFFICULTY_TITLE_EASY,
                                            IDS_DIFFICULTY_TITLE_NORMAL, IDS_DIFFICULTY_TITLE_HARD };
            return [NSString stringWithFormat:@"%@: %@", MCLEStr(IDS_SLIDER_DIFFICULTY), MCLEStr(kTitles[MAX(0, MIN(value, 3))])];
        }
    } else if ([scene isEqualToString:@"SettingsGraphicsMenu1080.swf"]) {
        if (sliderId == 3) {
            // LevelToDistance
            static const int kDistance[6] = {2, 4, 8, 16, 32, 64};
            return [NSString stringWithFormat:@"Render Distance: %d", kDistance[MAX(0, MIN(value, 5))]];
        }
        if (sliderId == 4) return [NSString stringWithFormat:@"%@: %d%%", MCLEStr(IDS_SLIDER_GAMMA), value];
        // sliderValueToFov: 70..110 over 0..100
        if (sliderId == 5) return [NSString stringWithFormat:@"FOV: %d", 70 + (MAX(0, MIN(value, 100)) * (110 - 70)) / 100];
        if (sliderId == 6) return [NSString stringWithFormat:@"%@: %d%%", MCLEStr(IDS_SLIDER_INTERFACEOPACITY), value];
    } else if ([scene isEqualToString:@"SettingsUIMenu1080.swf"]) {
        if (sliderId == 6) return [NSString stringWithFormat:@"%@: %d", MCLEStr(IDS_SLIDER_UISIZE), value];
        if (sliderId == 7) return [NSString stringWithFormat:@"%@: %d", MCLEStr(IDS_SLIDER_UISIZESPLITSCREEN), value];
    }
    return nil;
}

// UIScene_SettingsOptionsMenu's difficulty description under the slider
static NSString* MCLEDifficultyText(int difficulty) {
    static const int kText[4] = { IDS_DIFFICULTY_PEACEFUL, IDS_DIFFICULTY_EASY, IDS_DIFFICULTY_NORMAL,
                                  IDS_DIFFICULTY_HARD };
    // <font color> with eHTMLColor_White
    return [NSString stringWithFormat:@"<font color=\"#%08x\">%@", 0xffffffffu, MCLEStr(kText[MAX(0, MIN(difficulty, 3))])];
}

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
        // the slider goes 1..3, the setting is that minus one
        if (sliderId == 6 || sliderId == 7) *outValue = (unsigned char)MAX(0, MIN(v - 1, 2));
        if (sliderId == 6) return MCLE_SETTING_UISize;
        if (sliderId == 7) return MCLE_SETTING_UISizeSplitscreen;
    }
    return -1;
}

static int mcle_map_checkbox(int boxId) {
    // UIScene_ControlsMenu::handleCheckboxToggled
    if ([g_current_scene_name isEqualToString:@"Controls1080.swf"]) {
        if (boxId == 3) return MCLE_SETTING_ControlInvertLook;
        if (boxId == 4) return MCLE_SETTING_ControlSouthPaw;
        return -1;
    }
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

// SettingsOptionsMenu's Languages button, which only the Xbox One build
// shows, is the port's Longer Loading Screens option: the label has the
// value and a press flips it
static const int kOptionsLanguagesControl = 6;   // eControl_Languages
static NSString* MCLELongerLoadingLabel(void) {
    return [NSString stringWithFormat:@"Longer Loading: %@",
            MCLEStr(mcle_settings_get_longer_loading() ? IDS_ON : IDS_OFF)];
}

static void MCLEToggleLongerLoading(void) {
    mcle_settings_set_longer_loading(!mcle_settings_get_longer_loading());
    MCLECallPath(@"Languages", @"SetLabel", @[MCLELongerLoadingLabel()]);
}

// UIScene_ControlsMenu. The labels and lines in e_Pad order.
extern "C" void mcle_controls_text(int layout, int creativeMode, int southPaw, int* out); // Game/MCLEControls.cpp
static const char* const kControlsLabels[19] = {
    "LabelBack", "LabelLT", "LabelLB", "LabelDPadLeft", "LabelDPadRight", "LabelDPadUp", "LabelDPadDown",
    "LabelLS_1", "LabelLS_2", "LabelStart", "LabelRT", "LabelRB", "LabelY", "LabelB", "LabelA", "LabelX",
    "LabelRS_1", "LabelRS_2", "LabelTouch",
};
static const char* const kControlsLines[19] = {
    "LineBack", "LineLT", "LineLB", "LineDpadLeft", "LineDpadRight", "LineDpadUp", "LineDpadDown",
    "LineL3", "LineLeftStick", "LineStart", "LineRT", "LineRB", "LineY", "LineB", "LineA", "LineX",
    "LineR3", "LineRightStick", "LineTouch",
};
static int g_controls_layout = 0;       // m_iCurrentNavigatedControlsLayout
static bool g_controls_creative = false; // m_bCreativeMode
static std::atomic<bool> g_controls_layout_changed{false};

static void MCLESetPathVisible(NSString* path, BOOL visible);
// platformskinHD.swf holds the PS4 skin, skinHDOrbis.swf
static BOOL g_platform_skin_orbis = NO;

// PositionAllText
static void MCLEControlsPositionAllText(void) {
    int text[19];
    mcle_controls_text(g_controls_layout, g_controls_creative ? 1 : 0,
                       mcle_settings_get(MCLE_SETTING_ControlSouthPaw), text);
    for (int i = 0; i < 19; ++i) {
        MCLECallPath(@(kControlsLabels[i]), @"SetLabel", @[text[i] >= 0 ? MCLEStr(text[i]) : @""]);
        MCLESetPathVisible(@(kControlsLines[i]), text[i] >= 0);
    }
    g_controls_layout_changed = false;
}

// "Current Layout : Layout n"
static NSString* MCLEControlsLayoutString(int scheme) {
    static const int kSchemeText[3] = { IDS_CONTROLS_SCHEME0, IDS_CONTROLS_SCHEME1, IDS_CONTROLS_SCHEME2 };
    return [NSString stringWithFormat:@"%@ : %@", MCLEStr(IDS_CURRENT_LAYOUT), MCLEStr(kSchemeText[MAX(0, MIN(scheme, 2))])];
}

// UIScene_ControlsMenu constructor
static void MCLEControlsInit(void) {
    // SetPlatform: the Windows64 picture, or the PS4 one when the PS4 skin is in
    MCLECallPath(@"", @"SetPlatform", @[@(g_platform_skin_orbis ? 3 : 0)]);

    bool notInGame = !mcle_world_is_ticking();
    MCLECallPath(@"Version", @"Init", @[notInGame ? @(VER_PRODUCTVERSION_STR) : @" "]);
    MCLEHudState st;
    mcle_hud_get_state(&st);
    g_controls_creative = !notInGame && st.valid && st.mayfly;

    MCLECallPath(@"Button1", @"Init", @[@"1", @0]);
    MCLECallPath(@"Button2", @"Init", @[@"2", @1]);
    MCLECallPath(@"Button3", @"Init", @[@"3", @2]);

    struct Cb { const char* name; int ids; int id; int setting; };
    const Cb checkboxes[] = {
        { "InvertLook", IDS_INVERT_LOOK, 3, MCLE_SETTING_ControlInvertLook },
        { "SouthPaw",   IDS_SOUTHPAW,    4, MCLE_SETTING_ControlSouthPaw },
    };
    // UIControl_CheckBox::init -> FJ_CheckBox.Init(label, id, checked)
    for (const auto& c : checkboxes) {
        MCLECallPath(@(c.name), @"Init", @[MCLEStr(c.ids), @(c.id), mcle_settings_get(c.setting) ? @YES : @NO]);
    }

    int selected = MAX(0, MIN((int)mcle_settings_get(MCLE_SETTING_ControlScheme), 2));
    MCLECallPath(@"CurrentLayout", @"Init", @[MCLEControlsLayoutString(selected)]);
    g_controls_layout = selected;
    MCLECallPath(@"", @"SetControllerLayout", @[@(selected)]);

    for (int i = 0; i < 19; ++i) {
        MCLECallPath(@(kControlsLabels[i]), @"Init", @[@""]);
        MCLESetPathVisible(@(kControlsLines[i]), NO);
    }
    MCLEControlsPositionAllText();
}

// UIScene_HowToPlay (Game/MCLEHowToPlay.cpp has the pages)
extern "C" int mcle_htp_num_pages(void);
extern "C" int mcle_htp_menu_page(int button);
extern "C" int mcle_htp_flash_page(int page);
extern "C" const char* mcle_htp_page_text(int page);
extern "C" int mcle_htp_label_count(void);
extern "C" const char* mcle_htp_label(int i, const char** name);
static int g_htp_page = 0; // m_eCurrPage

static void MCLESetTooltips(const int ids[11]);
static void MCLETooltipsApplyOpacity(bool menuDisplayed, int opacityTimer, bool force);

// StartPage, then updateTooltips: Next unless it's the last page, Prev
// unless it's the first
static void MCLEHowToPlayStartPage(int page) {
    g_htp_page = page;
    NSString* text = [NSString stringWithUTF8String:mcle_htp_page_text(page)];
    MCLECallPath(@"", @"LoadHowToPlayPage", @[@(mcle_htp_flash_page(page)), text ? text : @""]);
    int iA = -1, iX = -1;
    if (page == 0) {
        iA = IDS_HOW_TO_PLAY_NEXT;
    } else if (page + 1 == mcle_htp_num_pages()) {
        iX = IDS_HOW_TO_PLAY_PREV;
    } else {
        iA = IDS_HOW_TO_PLAY_NEXT;
        iX = IDS_HOW_TO_PLAY_PREV;
    }
    int ids[11] = { iA, IDS_TOOLTIPS_BACK, iX, -1, -1, -1, -1, -1, -1, -1, -1 };
    MCLESetTooltips(ids);
}

// UIScene_HowToPlay constructor
static void MCLEHowToPlayInit(void) {
    for (int i = 0; i < mcle_htp_label_count(); ++i) {
        const char* name = "";
        NSString* text = [NSString stringWithUTF8String:mcle_htp_label(i, &name)];
        MCLECallPath(@(name), @"Init", @[text ? text : @""]);
    }
    MCLEHowToPlayStartPage(g_htp_page);
}

// CMinecraftApp::SetAutosaveTimerTime: the Autosave setting is in 15
// minute steps
static CFTimeInterval g_autosave_due = 0;
static void MCLESetAutosaveTimerTime(void) {
    g_autosave_due = CACurrentMediaTime() + mcle_settings_get(MCLE_SETTING_Autosave) * 60.0 * 15.0;
}

// UIScene_Credits
extern "C" int mcle_credits_count(void);
extern "C" int mcle_credits_get(int index, char* out, int cap, int* type);
static std::atomic<bool> g_credits_add_next{false};
static int g_credits_index = -1;

// setNextLabel / addImage: the last one tells the movie the credits end there
static void MCLECreditsAdd(int index) {
    char text[512];
    int type = 0;
    int kind = mcle_credits_get(index, text, sizeof(text), &type);
    bool last = index == mcle_credits_count() - 1;
    if (kind == 1) {
        NSString* label = [NSString stringWithUTF8String:text];
        MCLECallPath(@"", @"SetNextLabel", @[label ? label : @"", @(type), last ? @YES : @NO]);
    } else if (kind == 2) {
        MCLECallPath(@"", @"AddImage", @[@(type), last ? @YES : @NO]);
    }
}

// UIScene_Credits constructor: the first 20 lines Flash can cope with
static void MCLECreditsInit(void) {
    g_credits_add_next = false;
    g_credits_index = -1;
    for (int i = 0; i < 20; ++i) {
        ++g_credits_index;
        MCLECreditsAdd(g_credits_index);
    }
}

// How much a 1920x1080 movie, all of TU19's 16:9 screen, grows about the
// centre to cover a screen of another shape
static float MCLEMovieCoverScale(CGSize view) {
    if (view.width <= 0 || view.height <= 0) return 1.0f;
    const float movieAspect = 1920.0f / 1080.0f;
    const float aspect = (float)(view.width / view.height);
    return std::max(aspect / movieAspect, movieAspect / aspect);
}

// How far a wider screen goes past each side of the 16:9 stage, in stage
// pixels
static float MCLEStageSideOverhang(CGSize view) {
    if (view.width <= 0 || view.height <= 0) return 0.0f;
    const float stageWidth = 1080.0f * (float)(view.width / view.height);
    return std::max(0.0f, (stageWidth - 1920.0f) * 0.5f);
}

// Strips the movie runs from one side of its stage to the other go on to
// the sides of a wider screen
static void MCLEExtendStripsToScreen(PlayerHandle* player, CGSize view, const int* depths, int count) {
    const float side = MCLEStageSideOverhang(view);
    if (!player || side <= 0.0f) return;
    for (int i = 0; i < count; ++i) {
        ruffle_ios_set_root_child_edge_extend_at_depth(player, depths[i], side + 1.0f, side + 1.0f);
    }
}

// CreditBackground fills the 1920x1080 movie, all of TU19's 16:9 screen.
// It's grown about the centre to fill a wider screen too, so nothing
// shows around it.
static void MCLECreditsCoverScreen(PlayerHandle* player, CGSize view) {
    if (!player || view.width <= 0 || view.height <= 0) return;
    const float s = MCLEMovieCoverScale(view);
    ruffle_ios_set_root_child_transform_at_depth(player, 1, s, s,
        960.0f * (1.0f - s), 540.0f * (1.0f - s));
}

// UIScene_Credits::tick, the next line once the movie asks for it
static void MCLECreditsTick(void) {
    if (!g_credits_add_next.exchange(false)) return;
    ++g_credits_index;
    if (g_credits_index >= mcle_credits_count()) g_credits_index = 0;
    MCLECreditsAdd(g_credits_index);
}

// UIScene_EndPoem::tick: the paragraph the movie asked for, and whether
// it's the last
static void MCLEEndPoemTick(void) {
    const int count = mcle_end_poem_paragraph_count();
    const int requested = g_endpoem_requested.load();
    if (requested < 0 || requested >= count) return;
    NSString* label = [NSString stringWithUTF8String:mcle_end_poem_paragraph(requested)];
    MCLECallPath(@"", @"SetNextLabel", @[label ?: @"", @(requested), requested == count - 1 ? @YES : @NO]);
    g_endpoem_requested = -1;
}

// A world name as the game's wide strings hold it
static std::wstring MCLEWideString(NSString* s) {
    NSData* d = [s dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
    if (!d.length) return std::wstring();
    return std::wstring(static_cast<const wchar_t*>(d.bytes), d.length / sizeof(wchar_t));
}

static NSString* MCLEStringFromWide(const std::wstring& w) {
    if (w.empty()) return @"";
    NSString* s = [[NSString alloc] initWithBytes:w.data()
                                           length:w.size() * sizeof(wchar_t)
                                         encoding:NSUTF32LittleEndianStringEncoding];
    return s ?: @"";
}

// A seed in UTF-16 units, Windows' wchar_t, so a text seed hashes the same
static std::wstring MCLEWideUTF16(NSString* s) {
    std::wstring w;
    for (NSUInteger i = 0; i < s.length; ++i) w.push_back(static_cast<wchar_t>([s characterAtIndex:i]));
    return w;
}

static NSString* MCLEStringFromUTF16(const std::wstring& w) {
    if (w.empty()) return @"";
    std::vector<unichar> units(w.begin(), w.end());
    return [NSString stringWithCharacters:units.data() length:units.size()];
}

// IUIScene_StartGame scenes aren't signed in to an online service, so
// multiplayer isn't allowed: the online params are off, and if the Online
// setting is on it was changed by the system
static void MCLEStartGameOffline(MCLELaunchMoreOptions& p) {
    p.bOnlineSettingChangedBySystem = mcle_settings_get(MCLE_SETTING_Online) != 0;
    p.bOnlineGame = false;
    p.bInviteOnly = false;
    p.bAllowFriendsOfFriends = false;
}

// UIScene_CreateWorldMenu constructor, the state that outlives its movie
static void MCLECreateWorldReset(void) {
    g_create_world_name = MCLEStr(IDS_DEFAULT_WORLD_NAME);
    g_create_game_mode_id = 0;   // GameType::SURVIVAL
    g_create_game_mode_creative = false;
    g_create_ignore_input = false;

    MCLELaunchMoreOptions p;
    p.bGenerateOptions = true;
    p.bStructures = true;
    p.bFlatWorld = false;
    p.bBonusChest = false;
    p.bPVP = true;
    p.bTrust = true;
    p.bFireSpreads = true;
    p.bHostPrivileges = false;
    p.bTNT = true;
    p.iPad = 0;

    p.bMobGriefing = true;
    p.bKeepInventory = false;
    p.bDoMobSpawning = true;
    p.bDoMobLoot = true;
    p.bDoTileDrops = true;
    p.bNaturalRegeneration = true;
    p.bDoDaylightCycle = true;
    MCLEStartGameOffline(p);
    g_start_options = p;

    // IUIScene_StartGame's constructor
    g_texture_pack_index = mcle_texture_pack_index(0);
    g_texture_pack_desc_displayed = false;
    // block input if we're waiting for DLC to install. The end of dlc mounting fills the list.
    g_texture_pack_waiting_for_dlc = mcle_dlc_start_install_process(0);
}

extern "C" void mcle_ios_settings_event_bridge(const char* method, double id, double value) {
    if (!method) return;
    if (strcmp(method, "handleCheckboxToggled") == 0) {
        // The scene over the game
        if (mcle_container_is_open() && !g_pending_dialog) {
            int cid = (int)id;
            bool checked = value != 0.0;
            dispatch_async(dispatch_get_main_queue(), ^{ mcle_container_checkbox_toggled(cid, checked); });
            return;
        }
        if ([g_current_scene_name isEqualToString:@"LaunchMoreOptionsMenu1080.swf"]) {
            int cid = (int)id;
            BOOL checked = value != 0.0;
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc moreOptionsCheckbox:cid checked:checked];
            });
            return;
        }
        int setting = mcle_map_checkbox((int)id);
        if (setting < 0) return;
        unsigned char v = (value != 0.0) ? 1 : 0;
        mcle_settings_set(setting, v);
        NSLog(@"[settings] %@ checkbox id=%d -> setting=%d val=%u",
              g_current_scene_name, (int)id, setting, v);
        // UIScene_ControlsMenu: southpaw swaps move and look
        if (setting == MCLE_SETTING_ControlSouthPaw) g_controls_layout_changed = true;
        // ActionGameSettings: the host's game options follow these in game
        if (setting == MCLE_SETTING_GamertagsVisible || setting == MCLE_SETTING_BedrockFog) {
            dispatch_async(dispatch_get_main_queue(), ^{ mcle_game_host_settings_changed(); });
        }
    } else if (strcmp(method, "handleSliderMove") == 0) {
        if ([g_current_scene_name isEqualToString:@"LoadMenu1080.swf"]) {
            int sid = (int)id;
            int v = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc loadMenuSlider:sid value:v];
            });
            return;
        }
        if ([g_current_scene_name isEqualToString:@"CreateWorldMenu1080.swf"]) {
            int sid = (int)id;
            int v = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc createWorldSlider:sid value:v];
            });
            return;
        }
        if ([g_current_scene_name isEqualToString:@"LaunchMoreOptionsMenu1080.swf"]) {
            int sid = (int)id;
            int v = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc moreOptionsSlider:sid value:v];
            });
            return;
        }
        unsigned char stored = 0;
        int setting = mcle_map_slider((int)id, value, &stored);
        if (setting < 0) return;
        mcle_settings_set(setting, stored);
        NSLog(@"[settings] %@ slider id=%d raw=%g -> setting=%d val=%u",
              g_current_scene_name, (int)id, value, setting, stored);

        // Apply live for the settings the audio engine cares about
        // so the Music / Sound sliders actually fade the playing
        // track rather than just writing to disk.
        // UIScene_SettingsOptionsMenu restarts the autosave timer
        if (setting == MCLE_SETTING_Autosave) {
            dispatch_async(dispatch_get_main_queue(), ^{ MCLESetAutosaveTimerTime(); });
        }
        // The tooltip strip takes the new Interface Opacity, with the
        // settings menu up
        if (setting == MCLE_SETTING_InterfaceOpacity) {
            dispatch_async(dispatch_get_main_queue(), ^{ MCLETooltipsApplyOpacity(true, 0, false); });
        }
        // ActionGameSettings -> Options::set: the game's sound engine
        // changes straight away too, paused or not
        if (setting == MCLE_SETTING_MusicVolume) {
            mcle_audio_set_music_volume(stored);
            mcle_sound_update_music_volume(stored / 100.0f);
        } else if (setting == MCLE_SETTING_SoundFXVolume) {
            mcle_audio_set_sfx_volume(stored);
            mcle_sound_update_sfx_volume(stored / 100.0f);
        }

        // Update the slider's displayed label. Console does this in
        // UIControl_Slider::handleSliderMove (line 108-111 of
        // UIControl_Slider.cpp) via setLabel with the format string
        // the scene registered. Mirror per-control here.
        extern PlayerHandle* g_ruffle_player;
        if (!g_ruffle_player) return;
        static NSDictionary<NSString*, NSDictionary<NSNumber*, NSString*>*>* kSliderNames = @{
            @"SettingsAudioMenu1080.swf":    @{ @0: @"Music", @1: @"Sound" },
            @"SettingsControlMenu1080.swf":  @{ @0: @"SensitivityInGame", @1: @"SensitivityInMenu" },
            @"SettingsOptionsMenu1080.swf":  @{ @5: @"Autosave", @7: @"Difficulty" },
            @"SettingsGraphicsMenu1080.swf": @{ @3: @"RenderDistance", @4: @"Gamma", @5: @"FOV", @6: @"InterfaceOpacity" },
            @"SettingsUIMenu1080.swf":       @{ @6: @"UISize", @7: @"UISizeSplitscreen" },
        };
        NSString* sliderName = kSliderNames[g_current_scene_name][@((int)id)];
        NSString* sliderLabel = MCLESettingsSliderLabel(g_current_scene_name, (int)id, (int)value);
        // UIScene_SettingsOptionsMenu::handleSliderMove also updates the
        // difficulty description
        if ([g_current_scene_name isEqualToString:@"SettingsOptionsMenu1080.swf"] && (int)id == 7) {
            NSString* text = MCLEDifficultyText((int)value);
            dispatch_async(dispatch_get_main_queue(), ^{
                MCLECallPath(@"DifficultyText", @"SetLabel", @[text]);
            });
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
        // The scene over the game (UIScene_SignEntryMenu, UIScene_Keyboard)
        if (mcle_container_is_open() && !g_pending_dialog) {
            int cid = (int)id;
            int child = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{ mcle_container_press(cid, child); });
            return;
        }
        // UIScene_Keyboard over a front end menu
        if (mcle_keyboard_front_end_is_open() && !g_pending_dialog) {
            int cid = (int)id;
            int child = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{ mcle_keyboard_front_end_press(cid, child); });
            return;
        }
        // UIScene_ControlsMenu::handlePress: the layout buttons pick the scheme
        if ([g_current_scene_name isEqualToString:@"Controls1080.swf"]) {
            int control = (int)id;
            if (control < 0 || control > 2) return;
            mcle_settings_set(MCLE_SETTING_ControlScheme, (unsigned char)control);
            dispatch_async(dispatch_get_main_queue(), ^{
                MCLECallPath(@"CurrentLayout", @"SetLabel", @[MCLEControlsLayoutString(control)]);
            });
            return;
        }
        // UIScene_SettingsOptionsMenu::handlePress, its only button
        if ([g_current_scene_name isEqualToString:@"SettingsOptionsMenu1080.swf"] && !g_pending_dialog) {
            if ((int)id != kOptionsLanguagesControl) return;
            dispatch_async(dispatch_get_main_queue(), ^{
                mcle_audio_play_ui_sfx("press", 1.0f, 1.0f);
                MCLEToggleLongerLoading();
            });
            return;
        }
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
        if ([g_current_scene_name isEqualToString:@"LoadOrJoinMenu1080.swf"] && !g_pending_dialog) {
            // m_bIgnoreInput, handleInput wouldn't have let the press through
            if (g_load_or_join_ignore_input) return;
            if (listId == 0) {
                // UIScene_LoadOrJoinMenu::handlePress: item 0 is Create New
                // World, then the level generators, then the saves.
                int defaultButtons = g_load_default_buttons;
                if (itemId == 0) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        MCLECreateWorldReset();
                        [g_active_vc navigateForwardTo:@"CreateWorldMenu1080.swf"];
                    });
                } else if (itemId < defaultButtons) {
                    // The tutorial's LoadLevelGen goes straight to the game
                    int lGenID = itemId - 1;
                    dispatch_async(dispatch_get_main_queue(), ^{
                        if (g_loading_world) return;
                        mcle_audio_play_ui_sfx("press", 1.0f, 1.0f);
                        [g_active_vc navigateForwardTo:@"FullscreenProgress1080.swf"];
                        if (mcle_game_start_level_gen(lGenID)) {
                            g_loading_world = true;
                        } else {
                            NSLog(@"[loadorjoin] level generator %d didn't start", lGenID);
                            [g_active_vc navigateBack];
                        }
                    });
                } else {
                    int saveIdx = itemId - defaultButtons;
                    dispatch_async(dispatch_get_main_queue(), ^{
                        g_load_save_index = saveIdx;
                        g_load_menu_fresh = true;
                        [g_active_vc navigateForwardTo:@"LoadMenu1080.swf"];
                    });
                }
            } else if (listId == 1) {
                // JoinList. iOS has no netcode yet, so this list
                // stays empty - log in case we get here anyway
                // so the event path is visible.
                NSLog(@"[loadorjoin] join list press idx=%d (no networking yet)",
                      itemId);
            }
        } else if ([g_current_scene_name isEqualToString:@"HowToPlayMenu1080.swf"]) {
            // UIScene_HowToPlayMenu::handlePress: the topic's page
            int page = mcle_htp_menu_page(itemId);
            dispatch_async(dispatch_get_main_queue(), ^{
                g_htp_page = page;
                [g_active_vc navigateForwardTo:@"HowToPlay1080.swf"];
            });
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
        } else if ([g_current_scene_name isEqualToString:@"LoadMenu1080.swf"] && !g_pending_dialog) {
            int cid = listId;
            int child = itemId;
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc loadMenuPress:cid child:child];
            });
        } else if ([g_current_scene_name isEqualToString:@"CreateWorldMenu1080.swf"] && !g_pending_dialog) {
            int cid = listId;
            int child = itemId;
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc createWorldPress:cid child:child];
            });
        } else if ([g_current_scene_name isEqualToString:@"LaunchMoreOptionsMenu1080.swf"] && !g_pending_dialog) {
            int cid = listId;
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc moreOptionsPress:cid];
            });
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
    } else if (strcmp(method, "handleFocusChange") == 0) {
        if (mcle_container_is_open() && !g_pending_dialog) {
            int cid = (int)id;
            int child = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{ mcle_container_focus_change(cid, child, false); });
            return;
        }
        if (mcle_keyboard_front_end_is_open() && !g_pending_dialog) {
            int cid = (int)id;
            int child = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{ mcle_keyboard_front_end_focus_change(cid, child, false); });
            return;
        }
        // UIScene_ControlsMenu::handleFocusChange: show the layout being
        // looked at, from tick
        if ([g_current_scene_name isEqualToString:@"Controls1080.swf"]) {
            int control = (int)id;
            if (control >= 0 && control <= 2) {
                g_controls_layout = control;
                g_controls_layout_changed = true;
            }
            return;
        }
        if ([g_current_scene_name isEqualToString:@"LaunchMoreOptionsMenu1080.swf"]) {
            int cid = (int)id;
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc moreOptionsFocus:cid];
            });
            return;
        }
        // UIScene_LoadOrJoinMenu::handleFocusChange
        if ([g_current_scene_name isEqualToString:@"LoadOrJoinMenu1080.swf"] && !g_pending_dialog) {
            int cid = (int)id;
            int child = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{ MCLELoadOrJoinFocusChange(cid, child); });
        }
    } else if (strcmp(method, "handleSelectionChanged") == 0) {
        // IUIScene_StartGame::handleSelectionChanged, the texture pack under the cursor
        if ([g_current_scene_name isEqualToString:@"CreateWorldMenu1080.swf"] ||
            [g_current_scene_name isEqualToString:@"LoadMenu1080.swf"]) {
            int selectedId = (int)id;
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc startGameSelectionChanged:selectedId];
            });
        }
    } else if (strcmp(method, "handleRequestMoreData") == 0) {
        // UIScene_Credits::handleRequestMoreData, picked up by its tick
        if ([g_current_scene_name isEqualToString:@"Credits1080.swf"]) g_credits_add_next = true;
        // UIScene_EndPoem::handleRequestMoreData(startIndex)
        if ([g_current_scene_name isEqualToString:@"EndPoem1080.swf"]) g_endpoem_requested = (int)id;
    } else if (strcmp(method, "handleAnimationEnd") == 0) {
        // UIScene_SkinSelectMenu::handleAnimationEnd only changes the
        // scene's own state, so it's safe from inside the movie's tick
        if ([g_current_scene_name isEqualToString:@"SkinSelectMenu1080.swf"]) mcle_skin_select_animation_end();
        // UIScene_Intro::handleAnimationEnd changes scene, so after the tick
        if ([g_current_scene_name isEqualToString:@"Intro1080.swf"]) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [g_active_vc introNavigate];
            });
        }
    } else if (strcmp(method, "handleInitFocus") == 0) {
        if (mcle_container_is_open() && !g_pending_dialog) {
            int cid = (int)id;
            int child = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{ mcle_container_focus_change(cid, child, true); });
            return;
        }
        if (mcle_keyboard_front_end_is_open() && !g_pending_dialog) {
            int cid = (int)id;
            int child = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{ mcle_keyboard_front_end_focus_change(cid, child, true); });
            return;
        }
        // UIScene::_handleInitFocus goes on to handleFocusChange
        if ([g_current_scene_name isEqualToString:@"LoadOrJoinMenu1080.swf"] && !g_pending_dialog) {
            int cid = (int)id;
            int child = (int)value;
            dispatch_async(dispatch_get_main_queue(), ^{ MCLELoadOrJoinFocusChange(cid, child); });
            return;
        }
        // LCE fires this once per scene when SetFocus(-1) finds
        // the tabIndex==1 child (FJ_Document.SetFocus line 81-160,
        // OnFocusChange(false) calls through to handleInitFocus).
        // Purely informational for now; logging helps verify the
        // SetFocus path is firing as expected.
        NSLog(@"[handleInitFocus] scene=%@ control=%d child=%d",
              g_current_scene_name, (int)id, (int)value);
    }
}
// IggyPlayerCallMethodRS on a value path, e.g. "MainPanel.GameName".
// Args: NSString -> string, @YES/@NO -> bool, other NSNumber -> number.
static int MCLECallPath(NSString* path, NSString* method, NSArray* args) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return 0;
    size_t n = args.count;
    std::vector<int> kinds(n);
    std::vector<double> nums(n);
    std::vector<const uint8_t*> strs(n);
    std::vector<size_t> lens(n);
    NSMutableArray* keep = [NSMutableArray array];
    for (size_t i = 0; i < n; ++i) {
        NSObject* a = args[i];
        if ([a isKindOfClass:[NSString class]]) {
            NSData* d = [(NSString*)a dataUsingEncoding:NSUTF8StringEncoding];
            [keep addObject:d];
            kinds[i] = 2;
            strs[i] = (const uint8_t*)d.bytes;
            lens[i] = d.length;
        } else if (CFGetTypeID((__bridge CFTypeRef)a) == CFBooleanGetTypeID()) {
            kinds[i] = 1;
            nums[i] = [(NSNumber*)a boolValue] ? 1.0 : 0.0;
        } else {
            kinds[i] = 0;
            nums[i] = [(NSNumber*)a doubleValue];
        }
    }
    const char* pth = path.UTF8String;
    const char* m = method.UTF8String;
    return ruffle_ios_call_child_path(g_ruffle_player,
        (const uint8_t*)pth, strlen(pth), (const uint8_t*)m, strlen(m),
        kinds.data(), nums.data(), strs.data(), lens.data(), n);
}

// IggyPlayerDispatchEventRS for UIScene::sendInputToMovie's key
static void MCLEEndPoemSendKey(int iggyKeyCode, int down) {
    extern PlayerHandle* g_ruffle_player;
    if (g_ruffle_player) ruffle_ios_player_key(g_ruffle_player, iggyKeyCode, down);
}

// UIScene_EndPoem::handleInput, which does nothing once m_bIgnoreInput is set
static void MCLEEndPoemInput(void) {
    static uint32_t s_prevButtons = 0;
    mcle_ios_pad_state pad = {};
    mcle_ios_input_poll(0, &pad);
    const uint32_t buttons = g_endpoem_ignore_input ? 0 : pad.buttons;
    mcle_end_poem_input(buttons, s_prevButtons, MCLEEndPoemSendKey);
    s_prevButtons = buttons;
}

// UIControl_SpaceIndicatorBar over the save list. Bar values are 0..1 of
// MAXIMUM_SAVE_STORAGE (4GB) like upstream.
static void MCLESelectSaveSize(int saveIndex) {
    const double kMax = 4.0 * 1024.0 * 1024.0 * 1024.0;
    double total = 0, offset = 0, size = 0;
    for (int i = 0; i < mcle_saves_count(); ++i) {
        double s = (double)mcle_saves_size(i);
        if (i == saveIndex) { offset = total / kMax; size = s / kMax; }
        total += s;
    }
    MCLECallPath(@"SaveSizeBar", @"setTotalSize", @[@(total / kMax)]);
    MCLECallPath(@"SaveSizeBar", @"setSaveGameSize", @[@(size)]);
    MCLECallPath(@"SaveSizeBar", @"setSaveGameOffset", @[@(offset)]);
}

// UIScene_LoadOrJoinMenu::DoesSavesListHaveFocus, a save rather than Create
// New World or a level generator
static bool MCLELoadOrJoinSavesListHasFocus(void) {
    return g_load_focus_control == 0 && g_load_save_list_index > g_load_default_buttons - 1;
}

// UIScene_LoadOrJoinMenu::updateTooltips. The join list is always empty and
// there's no party, and no mash-up worlds are listed to Hide. Saving is
// never disabled and EnoughSpaceForAMinSaveGame is always true in the
// Windows64 storage, so a save gets Save Options. X is Change Storage
// Device, which SetTooltips takes off.
static void MCLELoadOrJoinTooltips(void) {
    int iRB = -1;
    if (MCLELoadOrJoinSavesListHasFocus() && g_load_default_buttons > 0 &&
        g_load_save_list_index >= g_load_default_buttons) {
        iRB = IDS_TOOLTIPS_SAVEOPTIONS;
    }
    int ids[11] = { IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK, IDS_TOOLTIPS_CHANGEDEVICE, -1, -1, -1, -1, iRB, -1, -1, -1 };
    MCLESetTooltips(ids);
}

// UIScene::_handleFocusChange then UIScene_LoadOrJoinMenu::handleFocusChange,
// from the movie's callback once it's done
static void MCLELoadOrJoinFocusChange(int controlId, int childId) {
    if (![g_current_scene_name isEqualToString:@"LoadOrJoinMenu1080.swf"] || g_pending_dialog) return;
    g_load_focus_control = controlId;
    if (controlId == 0) {   // eControl_SavesList
        g_load_save_list_index = childId;
        // m_bUpdateSaveSize, the bar shows the focused save
        MCLESelectSaveSize(childId - g_load_default_buttons);
    }
    MCLELoadOrJoinTooltips();
}

static void MCLESetPathVisible(NSString* path, BOOL visible) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    const char* pth = path.UTF8String;
    ruffle_ios_set_child_path_visible(g_ruffle_player, (const uint8_t*)pth, strlen(pth), visible ? 1 : 0);
}

// UIComponent_PressStartToPlay's controls: a call or setVisible on its movie
static void MCLEPressStartCall(NSString* path, NSString* method, NSArray* args) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    size_t n = args.count;
    std::vector<int> kinds(n);
    std::vector<double> nums(n);
    std::vector<const uint8_t*> strs(n);
    std::vector<size_t> lens(n);
    NSMutableArray* keep = [NSMutableArray array];
    for (size_t i = 0; i < n; ++i) {
        if ([args[i] isKindOfClass:[NSString class]]) {
            NSData* d = [(NSString*)args[i] dataUsingEncoding:NSUTF8StringEncoding];
            [keep addObject:d];
            kinds[i] = 2;
            strs[i] = (const uint8_t*)d.bytes;
            lens[i] = d.length;
        } else {
            kinds[i] = 0;
            nums[i] = [(NSNumber*)args[i] doubleValue];
        }
    }
    const char* pth = path.UTF8String;
    const char* m = method.UTF8String;
    ruffle_ios_call_sibling_path(g_ruffle_player, kPressStartToPlayDepth, (const uint8_t*)pth, strlen(pth),
                                 (const uint8_t*)m, strlen(m), kinds.data(), nums.data(), strs.data(), lens.data(), n);
}

static void MCLEPressStartVisible(NSString* path, BOOL visible) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    const char* pth = path.UTF8String;
    ruffle_ios_set_sibling_path_visible(g_ruffle_player, kPressStartToPlayDepth, (const uint8_t*)pth, strlen(pth),
                                        visible ? 1 : 0);
}

// UIComponent_PressStartToPlay constructor, once its movie is up
static void MCLEPressStartToPlayInit(void) {
    MCLEPressStartCall(@"", @"SetSafeZone", @[@(kSafeZoneTop), @(kSafeZoneBottom), @(kSafeZoneLeft), @(kSafeZoneRight)]);
    MCLEPressStartCall(@"TrialTimer", @"Init", @[@""]);
    MCLEPressStartVisible(@"TrialTimer", NO);
    MCLEPressStartCall(@"MainPanel.PressStartLabel", @"Init", @[MCLEStr(IDS_PRESS_START_TO_JOIN)]);
    MCLEPressStartVisible(@"SaveIcon", NO);
    MCLEPressStartVisible(@"MainPanel", NO);
    MCLEPressStartVisible(@"PlayerName", NO);
}

// UIController::ShowAutosaveCountdownTimer, the component's showTrialTimer
static bool g_autosave_countdown_shown = false;
static void MCLEShowAutosaveCountdownTimer(bool show) {
    if (show == g_autosave_countdown_shown) return;
    g_autosave_countdown_shown = show;
    MCLEPressStartVisible(@"TrialTimer", show);
}

// UIController::UpdateAutosaveCountdownTimer: IDS_AUTOSAVE_COUNTDOWN with
// the seconds, the component's setTrialTimer
static void MCLEUpdateAutosaveCountdownTimer(unsigned int seconds) {
    static int s_shown = -1;
    if ((int)seconds == s_shown) return;
    s_shown = (int)seconds;
    NSString* text = [MCLEStr(IDS_AUTOSAVE_COUNTDOWN) stringByReplacingOccurrencesOfString:@"%d"
                                                                               withString:@(seconds).stringValue];
    MCLEPressStartCall(@"TrialTimer", @"SetLabel", @[text]);
}

// UIComponent_Panorama::tick: night once the loaded overworld is past 14000
// in its day, day otherwise (no level, or the Nether / End)
static void MCLEPanoramaTick(void) {
    extern PlayerHandle* g_ruffle_player;
    static bool s_showingDay = true;
    long long gameTime = 0;
    int dimension = 0;
    long long timeOfDay = 0;
    if (mcle_world_game_time(&gameTime, &dimension) && dimension == 0) timeOfDay = gameTime % 24000;
    bool day = timeOfDay <= 14000;
    if (day != s_showingDay && g_ruffle_player) {
        s_showingDay = day;
        double arg = day ? 1.0 : 0.0;
        ruffle_ios_call_method_on_sibling_root(g_ruffle_player, -2, (const uint8_t*)"ShowPanoramaDay", 15, &arg, 1);
    }
}

// UIScene_HUD. What was last sent to the movie, starting from the values
// IUIScene_HUD's constructor sets, so each setter only calls in on a change.
static struct {
    int activeSlot, guiScale, expLevel, maxHealth, food, air, armour, saturation, healthAbsorb;
    float expProgress;
    bool healthBlink, healthPoison, healthWither, foodPoison, regen, visible;
    bool showHealth, showHorseHealth, showFood, showAir, showArmour, showExpBar;
    bool ridingHorse, isJumpable, tooltipsVisible;
    int horseMaxHealth, horseHealth;
    float horseJumpProgress;
    char displayName[64];
    int labelCalls, hideLabelCalls;
    float opacity;
    float lastDragonHealth;
    bool showDragonHealth;
    int ticksWithNoBoss;
} g_hud;

static void MCLEHudChatReset(void);

static void MCLEHudReset(void) {
    g_hud.activeSlot = -1;
    g_hud.guiScale = -1;
    g_hud.expProgress = 0.0f;
    g_hud.expLevel = 0;
    g_hud.maxHealth = 20;
    g_hud.healthBlink = false;
    g_hud.healthPoison = false;
    g_hud.healthWither = true;
    g_hud.food = -1;
    g_hud.foodPoison = false;
    g_hud.air = 10;
    g_hud.armour = 0;
    g_hud.showHealth = g_hud.showFood = g_hud.showAir = g_hud.showArmour = g_hud.showExpBar = true;
    g_hud.showHorseHealth = true;
    g_hud.ridingHorse = true;
    g_hud.isJumpable = false;
    g_hud.horseHealth = 1;
    g_hud.horseMaxHealth = -1;
    g_hud.horseJumpProgress = 1.0f;
    g_hud.tooltipsVisible = true;
    g_hud.displayName[0] = 0;
    g_hud.regen = false;
    g_hud.saturation = 0;
    g_hud.healthAbsorb = -1;
    g_hud.visible = true;
    // Label calls from before this scene don't carry over
    MCLEHudState st;
    mcle_hud_get_state(&st);
    g_hud.labelCalls = st.labelCalls;
    g_hud.hideLabelCalls = st.hideLabelCalls;
    // UIScene sends the opacity once a scene has loaded
    g_hud.opacity = -1.0f;
    g_hud.lastDragonHealth = 0.0f;
    g_hud.showDragonHealth = false;
    g_hud.ticksWithNoBoss = 0;
    MCLEHudChatReset();
}

// UIScene_HUD::tick: the boss's health bar
static void MCLEHudBossTick(void) {
    float health = 0.0f;
    char name[128];
    if (!mcle_hud_boss(&health, name, sizeof(name))) {
        if (g_hud.showDragonHealth) {
            // No boss and health is visible
            if (g_hud.ticksWithNoBoss <= 20) {
                ++g_hud.ticksWithNoBoss;
            } else {
                // ShowDragonHealth(false)
                g_hud.showDragonHealth = false;
                MCLECallPath(@"", @"ShowDragonHealth", @[@NO]);
            }
        }
    } else {
        g_hud.ticksWithNoBoss = 0;
        // SetDragonHealth
        if (health != g_hud.lastDragonHealth) {
            g_hud.lastDragonHealth = health;
            MCLECallPath(@"", @"SetDragonHealth", @[@(health)]);
        }
        if (!g_hud.showDragonHealth) {
            NSString* label = [NSString stringWithUTF8String:name];
            MCLECallPath(@"", @"SetDragonLabel", @[label ?: @""]);
            g_hud.showDragonHealth = true;
            MCLECallPath(@"", @"ShowDragonHealth", @[@YES]);
        }
    }
}

static void MCLEHudShow(NSString* fn, bool show, bool& last) {
    if (show != last) {
        last = show;
        MCLECallPath(@"", fn, @[show ? @YES : @NO]);
    }
}

static void MCLEHudNumber(NSString* fn, int value, int& last) {
    if (value != last) {
        last = value;
        MCLECallPath(@"", fn, @[@(value)]);
    }
}

// UIScene_HUD::SetRidingHorse
static void MCLEHudSetRidingHorse(bool ridingHorse, bool isJumpable, int maxHorseHealth) {
    if (g_hud.ridingHorse != ridingHorse || maxHorseHealth != g_hud.horseMaxHealth) {
        g_hud.ridingHorse = ridingHorse;
        g_hud.isJumpable = isJumpable;
        g_hud.horseMaxHealth = maxHorseHealth;
        MCLECallPath(@"", @"SetRidingHorse", @[ridingHorse ? @YES : @NO, isJumpable ? @YES : @NO, @(maxHorseHealth)]);
    }
}

// UIScene_HUD::SetHealthAbsorb
static void MCLEHudSetHealthAbsorb(int healthAbsorb) {
    if (g_hud.healthAbsorb != healthAbsorb) {
        g_hud.healthAbsorb = healthAbsorb;
        MCLECallPath(@"", @"SetHealthAbsorb", @[healthAbsorb > 0 ? @YES : @NO, @(healthAbsorb)]);
    }
}

// IUIScene_HUD::renderPlayerHealth
static void MCLEHudRenderPlayerHealth(const MCLEHudState& st) {
    MCLEHudShow(@"ShowHealth", true, g_hud.showHealth);
    MCLEHudShow(@"SetRegenerationEffect", st.regen != 0, g_hud.regen);

    // SetHealth: the bigger of now and last, so a hit flashes the lost hearts
    int maxHealth = std::max(st.health, st.lastHealth);
    bool blink = st.blink != 0, poison = st.poison || st.wither, wither = st.wither != 0;
    if (maxHealth != g_hud.maxHealth || blink != g_hud.healthBlink ||
        poison != g_hud.healthPoison || wither != g_hud.healthWither) {
        g_hud.maxHealth = maxHealth;
        g_hud.healthBlink = blink;
        g_hud.healthPoison = poison;
        g_hud.healthWither = wither;
        MCLECallPath(@"", @"SetHealth", @[@(maxHealth), blink ? @YES : @NO, poison ? @YES : @NO,
                                          wither ? @YES : @NO]);
    }
    MCLEHudSetHealthAbsorb((int)st.absorption);

    if (st.armour > 0) {
        MCLEHudShow(@"ShowArmour", true, g_hud.showArmour);
        MCLEHudNumber(@"SetArmour", st.armour, g_hud.armour);
    } else {
        MCLEHudShow(@"ShowArmour", false, g_hud.showArmour);
    }

    if (st.riding != 2) {
        MCLEHudSetRidingHorse(false, false, 0);

        MCLEHudShow(@"ShowFood", true, g_hud.showFood);
        MCLEHudShow(@"ShowHorseHealth", false, g_hud.showHorseHealth);
        g_hud.horseHealth = 0;

        // Food doesn't flash, so only the current level counts
        bool hunger = st.hunger != 0;
        if (st.food != g_hud.food || hunger != g_hud.foodPoison) {
            g_hud.food = st.food;
            g_hud.foodPoison = hunger;
            MCLECallPath(@"", @"SetFood", @[@(st.food), hunger ? @YES : @NO]);
        }
        MCLEHudNumber(@"SetFoodSaturationLevel", st.saturation, g_hud.saturation);

        if (st.underWater) {
            MCLEHudShow(@"ShowAir", true, g_hud.showAir);
            MCLEHudNumber(@"SetAir", st.air, g_hud.air);
        } else {
            MCLEHudShow(@"ShowAir", false, g_hud.showAir);
        }
    } else {
        MCLEHudSetRidingHorse(true, st.ridingJumpable != 0, st.riderMaxHealth);
        // SetHorseHealth
        if (g_hud.ridingHorse && g_hud.horseHealth != st.riderHealth) {
            g_hud.horseHealth = st.riderHealth;
            MCLECallPath(@"", @"SetHorseHealth", @[@(st.riderHealth), @NO]);
        }
        MCLEHudShow(@"ShowHorseHealth", true, g_hud.showHorseHealth);
    }
}

extern "C" float mcle_hud_chat_line(int index, char *out, int cap);
extern "C" float mcle_hud_jukebox(char *out, int cap);

// UIControl::setOpacity and setLabel on the HUD's chat lines, what was last
// sent starting from UIControl's 1.0 / unset
static struct {
    float labelOpacity[10], backgroundOpacity[10], jukeboxOpacity;
    char label[10][256], jukebox[256];
    bool labelSet[10], jukeboxSet;
    double lastTimer;
} g_hudChat;

static void MCLEHudChatReset(void) {
    for (int i = 0; i < 10; ++i) {
        g_hudChat.labelOpacity[i] = 1.0f;
        g_hudChat.backgroundOpacity[i] = 1.0f;
        g_hudChat.labelSet[i] = false;
    }
    g_hudChat.jukeboxOpacity = 1.0f;
    g_hudChat.jukeboxSet = false;
    g_hudChat.lastTimer = 0;
}

static void MCLEHudControlAlpha(NSString* control, float opacity, float& last) {
    if (opacity != last) {
        last = opacity;
        MCLECallPath(@"", @"SetControlAlpha", @[control, @(opacity)]);
    }
}

static void MCLEHudLabel(NSString* control, const char* text, char* last, bool& set) {
    if (!set || strcmp(text, last) != 0) {
        set = true;
        snprintf(last, 256, "%s", text);
        NSString* str = [NSString stringWithUTF8String:text];
        MCLECallPath(control, @"SetLabel", @[str ? str : @""]);
    }
}

// UIScene_HUD::handleTimerComplete, every 100ms (addTimer(0, 100)). Each
// line shows while its message is fading, Win64's Gui::render chat aside.
static void MCLEHudChatTimer(void) {
    double now = CACurrentMediaTime();
    if (now - g_hudChat.lastTimer < 0.1) return;
    g_hudChat.lastTimer = now;
    char text[256];
    for (int i = 0; i < 10; ++i) {
        NSString* label = [NSString stringWithFormat:@"Label%d", i + 1];
        NSString* background = [NSString stringWithFormat:@"Label%dBackground", i + 1];
        float opacity = mcle_hud_chat_line(i, text, sizeof(text));
        if (opacity > 0) {
            MCLEHudControlAlpha(background, opacity, g_hudChat.backgroundOpacity[i]);
            MCLEHudControlAlpha(label, opacity, g_hudChat.labelOpacity[i]);
            MCLEHudLabel(label, text, g_hudChat.label[i], g_hudChat.labelSet[i]);
        } else {
            MCLEHudControlAlpha(background, 0, g_hudChat.backgroundOpacity[i]);
            MCLEHudControlAlpha(label, 0, g_hudChat.labelOpacity[i]);
            MCLEHudLabel(label, "", g_hudChat.label[i], g_hudChat.labelSet[i]);
        }
    }
    float jukebox = mcle_hud_jukebox(text, sizeof(text));
    MCLEHudControlAlpha(@"Jukebox", jukebox, g_hudChat.jukeboxOpacity);
    MCLEHudLabel(@"Jukebox", text, g_hudChat.jukebox, g_hudChat.jukeboxSet);
}

// IUIScene_HUD::updateFrameTick, called every frame in game
static void MCLEHudPump(void) {
    MCLEHudState st;
    mcle_hud_get_state(&st);
    if (!st.valid) return;

    MCLEHudBossTick();
    MCLEHudChatTimer();

    // SetHudSize -> LoadHud
    MCLEHudNumber(@"LoadHud", mcle_settings_get(MCLE_SETTING_UISize), g_hud.guiScale);

    // SetDisplayName
    if (strcmp(st.displayName, g_hud.displayName) != 0) {
        snprintf(g_hud.displayName, sizeof(g_hud.displayName), "%s", st.displayName);
        NSString* name = [NSString stringWithUTF8String:st.displayName];
        MCLECallPath(@"", @"SetDisplayName", @[name ? name : @""]);
    }

    // SetTooltipsEnabled, with no menu up while this runs
    MCLEHudShow(@"SetTooltipsEnabled", mcle_settings_get(MCLE_SETTING_Tooltips) != 0, g_hud.tooltipsVisible);

    MCLEHudNumber(@"SetActiveSlot", st.activeSlot, g_hud.activeSlot);

    if (st.canHurtPlayer) {
        MCLEHudRenderPlayerHealth(st);
    } else {
        MCLEHudSetRidingHorse(st.riding != 0, st.riding != 0 && st.ridingJumpable != 0, 0);
        MCLEHudShow(@"ShowHorseHealth", false, g_hud.showHorseHealth);
        g_hud.horseHealth = 0;
        MCLEHudShow(@"ShowHealth", false, g_hud.showHealth);
        MCLEHudShow(@"ShowFood", false, g_hud.showFood);
        MCLEHudShow(@"ShowAir", false, g_hud.showAir);
        MCLEHudShow(@"ShowArmour", false, g_hud.showArmour);
        MCLEHudShow(@"ShowExpBar", false, g_hud.showExpBar);
        MCLEHudSetHealthAbsorb(0);
    }

    if (st.ridingJumpable) {
        // SetHorseJumpBarProgress
        if (g_hud.ridingHorse && g_hud.horseJumpProgress != st.jumpRidingScale) {
            g_hud.horseJumpProgress = st.jumpRidingScale;
            MCLECallPath(@"", @"SetHorseJumpBarProgress", @[@(st.jumpRidingScale)]);
        }
    } else if (st.hasExperience) {
        MCLEHudShow(@"ShowExpBar", true, g_hud.showExpBar);
        if (st.expProgress != g_hud.expProgress) {
            g_hud.expProgress = st.expProgress;
            MCLECallPath(@"", @"SetExpBarProgress", @[@(st.expProgress)]);
        }
        MCLEHudNumber(@"SetPlayerLevel", st.expLevel, g_hud.expLevel);
    } else {
        MCLEHudShow(@"ShowExpBar", false, g_hud.showExpBar);
        MCLEHudNumber(@"SetPlayerLevel", 0, g_hud.expLevel);
    }

    // A hide comes two seconds after its label, so when both are new the
    // hide is the older one
    if (st.hideLabelCalls != g_hud.hideLabelCalls) {
        g_hud.hideLabelCalls = st.hideLabelCalls;
        MCLECallPath(@"", @"HideSelectedLabel", @[]);
    }
    if (st.labelCalls != g_hud.labelCalls) {
        g_hud.labelCalls = st.labelCalls;
        NSString* label = [NSString stringWithUTF8String:st.label];
        MCLECallPath(@"", @"SetSelectedLabel", @[label ? label : @""]);
    }

    // Interface Opacity, back at the default 80% for a while after the
    // hotbar is scrolled. No menu is up while this runs.
    {
        unsigned char ucAlpha = mcle_settings_get(MCLE_SETTING_InterfaceOpacity);
        float fVal;
        if (ucAlpha < 80) {
            if (st.opacityTimer != 0) {
                if (st.opacityTimer < 10) {
                    float fStep = (80.0f - static_cast<float>(ucAlpha)) / 10.0f;
                    fVal = 0.01f * (80.0f - ((10.0f - static_cast<float>(st.opacityTimer)) * fStep));
                } else {
                    fVal = 0.01f * 80.0f;
                }
            } else {
                fVal = 0.01f * static_cast<float>(ucAlpha);
            }
        } else {
            fVal = 0.01f * static_cast<float>(ucAlpha);
        }
        // UIScene::setOpacity
        if (fVal != g_hud.opacity) {
            g_hud.opacity = fVal;
            MCLECallPath(@"", @"SetAlpha", @[@(fVal)]);
        }
        // The tooltip strip follows the same timer, sent when it changes
        MCLETooltipsApplyOpacity(false, st.opacityTimer, false);
    }

    // SetVisible: shown while playing with Display HUD on
    bool visible = mcle_settings_get(MCLE_SETTING_DisplayHUD) != 0;
    if (visible != g_hud.visible) {
        g_hud.visible = visible;
        MCLESetPathVisible(@"", visible);
    }
}

// UIComponent_Tooltips, the one strip the menus and the HUD share. What
// each slot shows, sent only when it changes like _SetTooltip, then one
// relayout. Slots go A, B, X, Y, LT, RT, LB, RB, LS, RS, Back. It starts
// out unknown so the first call sends every slot.
static struct {
    int ids[11];
    bool shown[11];
    int enabled; // in game: the In-Game Tooltips setting last applied, -1 unknown
    bool known;
} g_tooltips;

static void MCLESetTooltips(const int in[11]) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    if (!g_tooltips.known) {
        for (int i = 0; i < 11; i++) {
            g_tooltips.ids[i] = -3;
            g_tooltips.shown[i] = true;
        }
        g_tooltips.known = true;
    }
    bool relayout = false;
    for (int i = 0; i < 11; i++) {
        int id = in[i];
        // UIController::SetTooltips strips out the storage device ones off
        // the 360
        if (i == 2 && (id == IDS_TOOLTIPS_SELECTDEVICE || id == IDS_TOOLTIPS_CHANGEDEVICE)) id = -1;
        if (id == g_tooltips.ids[i] && !(id > -1 && !g_tooltips.shown[i])) continue;
        g_tooltips.ids[i] = id;
        relayout = true;
        bool show = id > -1 || id == -2;
        // _SetTooltip skips hiding one that isn't showing
        if (!show && !g_tooltips.shown[i]) continue;
        g_tooltips.shown[i] = show;
        const char* label = id > -1 ? mcle_string(id) : "";
        ruffle_ios_call_set_tooltip(g_ruffle_player, kTooltipsDepth, i, (const uint8_t*)label, strlen(label),
                                    show ? 1 : 0);
    }
    if (relayout) {
        const char* updateName = "UpdateLayout";
        ruffle_ios_call_method_on_sibling_root(g_ruffle_player, kTooltipsDepth, (const uint8_t*)updateName,
                                               strlen(updateName), NULL, 0);
    }
}

// UIController::AnimateKeyPress: a press goes into the tooltips movie
// through UIComponent_Tooltips::handleInput, and its glyph animates
static void MCLEAnimateKeyPress(int iggyKeyCode) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player || iggyKeyCode < 0) return;
    ruffle_ios_sibling_key(g_ruffle_player, kTooltipsDepth, iggyKeyCode, 1);
}

// enterWorldUI hides the strip, the next pump puts it back per the setting
static void MCLEGameTooltipsReset(void) {
    g_tooltips.enabled = -1;
}

// UIComponent_Tooltips::tick: the strip follows Interface Opacity, back at
// 80% for a while after the hotbar scrolls, and never under 15% with a menu
// up. setOpacity only calls the movie when it changes, or when the scene is
// fresh (force). It's sent when a scene seeds the strip, when the setting
// changes, and from the HUD pump in game, never polled.
static float g_tooltips_alpha = -1.0f;
static void MCLETooltipsApplyOpacity(bool menuDisplayed, int opacityTimer, bool force) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    unsigned char ucAlpha = mcle_settings_get(MCLE_SETTING_InterfaceOpacity);
    if (menuDisplayed && ucAlpha < 15) ucAlpha = 15;
    float fVal;
    if (ucAlpha < 80 && opacityTimer != 0) {
        if (opacityTimer < 10) {
            float fStep = (80.0f - static_cast<float>(ucAlpha)) / 10.0f;
            fVal = 0.01f * (80.0f - ((10.0f - static_cast<float>(opacityTimer)) * fStep));
        } else {
            fVal = 0.01f * 80.0f;
        }
    } else {
        fVal = 0.01f * static_cast<float>(ucAlpha);
    }
    if (!force && fVal == g_tooltips_alpha) return;
    g_tooltips_alpha = fVal;
    const char* setAlpha = "SetAlpha";
    double value = fVal;
    ruffle_ios_call_method_on_sibling_root(g_ruffle_player, kTooltipsDepth,
        (const uint8_t*)setAlpha, strlen(setAlpha), &value, 1);
}

// The strip's movie went up again on a new skin, like UIComponent_Tooltips
// in UIController::ReloadSkin's ReloadAll: it gets back the safe zone,
// opacity and buttons it had
static void MCLETooltipsMovieReloaded(void) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    const char* setSafe = "SetSafeZone";
    double safe[4] = { kSafeZoneTop, kSafeZoneBottom, kSafeZoneLeft, kSafeZoneRight };
    ruffle_ios_call_method_on_sibling_root(g_ruffle_player, kTooltipsDepth,
        (const uint8_t*)setSafe, strlen(setSafe), safe, 4);
    if (g_tooltips_alpha >= 0.0f) {
        const char* setAlpha = "SetAlpha";
        double value = g_tooltips_alpha;
        ruffle_ios_call_method_on_sibling_root(g_ruffle_player, kTooltipsDepth,
            (const uint8_t*)setAlpha, strlen(setAlpha), &value, 1);
    }
    if (g_tooltips.known) {
        int ids[11];
        std::copy(g_tooltips.ids, g_tooltips.ids + 11, ids);
        g_tooltips.known = false;
        MCLESetTooltips(ids);
    }
    // In game the HUD pump shows it again per the setting
    g_tooltips.enabled = -1;
}

// In game: Minecraft::tick's tooltips
static void MCLEGameTooltipsPump(void) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    // UIComponent_Tooltips::render: with no menu up the strip is off when
    // In-Game Tooltips or Display HUD is
    int enabled = (mcle_settings_get(MCLE_SETTING_Tooltips) != 0 &&
                   mcle_settings_get(MCLE_SETTING_DisplayHUD) != 0) ? 1 : 0;
    if (enabled != g_tooltips.enabled) {
        g_tooltips.enabled = enabled;
        ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTooltipsDepth, enabled);
    }

    int ids[11];
    mcle_tooltips_get(ids);
    ids[10] = -1;
    MCLESetTooltips(ids);
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
// Ruffle draws here, transparent, above the world in metalView
@property (strong, nonatomic) MetalView* overlayView;
// Above Ruffle: what Iggy's custom draw callbacks draw (hotbar items)
@property (strong, nonatomic) MetalView* customDrawView;
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
// Windows64 clears to white outside a game, which is what shows around
// the intro's logos. Under the SWF layer while the intro is up.
@property (strong, nonatomic) UIView* introBackdrop;
// Ordered stack of scene entries the player navigated through,
// oldest at index 0. Each entry is a dict with @"swf" (NSString)
// and @"focus" (NSNumber int) so B-back restores both the scene
// and which button the user had selected before going deeper.
// Mirrors how UIController::NavigateBack walks the scene group
// stack on console (Common/UI/UIController.cpp:2006) without
// porting the full UIGroup machinery.
@property (strong, nonatomic) NSMutableArray<NSDictionary*>* menuStack;
@end

// UIController::loadSkins, Windows64's: each skin library from the selected
// texture pack's media archive when it has one (CMinecraftApp::getArchiveFile),
// else the game's own in Documents. The movies loaded from now on import
// these. A library goes back to the file only after a pack replaced it,
// or for the platform skins when the pad gave them another skin.
static void MCLEReloadSkinLibraries(bool platformChanged = false) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (!docs) return;
    // The platform skins are the ones their aliases were made from
    NSString* (^platformSkin)(NSString*, NSString*) = ^NSString*(NSString* alias, NSString* skin) {
        NSString* marker = [[docs stringByAppendingPathComponent:alias] stringByAppendingString:@".source"];
        NSString* source = [NSString stringWithContentsOfFile:marker encoding:NSUTF8StringEncoding error:nil];
        return source.length ? source : skin;
    };
    NSArray<NSArray<NSString*>*>* skins = @[
        @[ platformSkin(@"platformskinHD.swf", @"skinHDWin.swf"), @"platformskinHD.swf" ],
        @[ platformSkin(@"platformskin.swf", @"skinWin.swf"), @"platformskin.swf" ],
        @[ @"skinGraphics.swf", @"skinGraphics.swf" ],
        @[ @"skinGraphicsHud.swf", @"skinGraphicsHud.swf" ],
        @[ @"skinGraphicsInGame.swf", @"skinGraphicsInGame.swf" ],
        @[ @"skinGraphicsTooltips.swf", @"skinGraphicsTooltips.swf" ],
        @[ @"skinGraphicsLabels.swf", @"skinGraphicsLabels.swf" ],
        @[ @"skinLabels.swf", @"skinLabels.swf" ],
        @[ @"skinInGame.swf", @"skinInGame.swf" ],
        @[ @"skinHud.swf", @"skinHud.swf" ],
        @[ @"skinTooltips.swf", @"skinTooltips.swf" ],
        @[ @"skin.swf", @"skin.swf" ],
        @[ @"skinHDGraphics.swf", @"skinHDGraphics.swf" ],
        @[ @"skinHDGraphicsHud.swf", @"skinHDGraphicsHud.swf" ],
        @[ @"skinHDGraphicsInGame.swf", @"skinHDGraphicsInGame.swf" ],
        @[ @"skinHDGraphicsTooltips.swf", @"skinHDGraphicsTooltips.swf" ],
        @[ @"skinHDGraphicsLabels.swf", @"skinHDGraphicsLabels.swf" ],
        @[ @"skinHDLabels.swf", @"skinHDLabels.swf" ],
        @[ @"skinHDInGame.swf", @"skinHDInGame.swf" ],
        @[ @"skinHDHud.swf", @"skinHDHud.swf" ],
        @[ @"skinHDTooltips.swf", @"skinHDTooltips.swf" ],
        @[ @"skinHD.swf", @"skinHD.swf" ],
    ];
    static NSMutableSet<NSString*>* fromPack = [NSMutableSet set];
    for (NSArray<NSString*>* skin in skins) {
        NSString* skinPath = skin[0];
        NSString* skinName = skin[1];
        std::vector<unsigned char> bytes;
        const bool packSkin = mcle_texture_pack_archive_file(MCLEWideString(skinPath), bytes);
        NSString* file = [docs stringByAppendingPathComponent:skinName];
        if (!packSkin) {
            const bool platform = platformChanged && [skinName hasPrefix:@"platformskin"];
            if (![fromPack containsObject:skinName] && !platform) continue;
            NSData* data = [NSData dataWithContentsOfFile:file];
            if (!data.length) continue;
            const unsigned char* p = static_cast<const unsigned char*>(data.bytes);
            bytes.assign(p, p + data.length);
        }
        NSString* url = [NSString stringWithFormat:@"file://%@",
                         [file stringByReplacingOccurrencesOfString:@" " withString:@"%20"]];
        int rc = ruffle_ios_cache_import(g_ruffle_player,
            (const uint8_t*)skinName.UTF8String, strlen(skinName.UTF8String),
            bytes.data(), bytes.size(),
            (const uint8_t*)url.UTF8String, strlen(url.UTF8String));
        if (rc != 1) continue;
        if (packSkin) [fromPack addObject:skinName];
        else [fromPack removeObject:skinName];
    }
}

@implementation MinecraftViewController

// Find a SWF to play. Preference order:
//   1. Documents/Intro1080.swf     (UIController::postInit's first scene)
//   2. Documents/MainMenu1080.swf  (user-supplied via Files app)
//   3. Documents/*.swf             (anything the user dropped in)
//   4. Bundled test_rect.swf       (built-in fallback)
- (NSString*)resolveSwfPath {
    NSFileManager* fm = NSFileManager.defaultManager;
    NSURL* docs = [fm URLsForDirectory:NSDocumentDirectory
                             inDomains:NSUserDomainMask].firstObject;
    NSString* docsPath = docs.path;

    if (docsPath.length) {
        // UIController::postInit navigates to eUIScene_Intro
        NSString* intro = [docsPath stringByAppendingPathComponent:@"Intro1080.swf"];
        if ([fm fileExistsAtPath:intro]) return intro;
        // Then MainMenu1080.swf.
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
// A PlayStation pad gets the PS4 skin (skinHDOrbis.swf / skinOrbis.swf,
// what CopyPlatformSkin.bat puts in place on that platform) when it's
// there, so the button glyphs match the pad. It's the pad that drives the
// game, pad 0. -1 while no pad is connected: GameController only reports
// the paired pads a moment after launch.
static int MCLEPadIsPlayStation(void) {
    GCController* pad = GCController.controllers.firstObject;
    if (!pad) return -1;
    if ([pad.extendedGamepad isKindOfClass:GCDualShockGamepad.class]) return 1;
    if (@available(iOS 14.5, *)) {
        if ([pad.extendedGamepad isKindOfClass:GCDualSenseGamepad.class]) return 1;
    }
    NSString* category = pad.productCategory ?: @"";
    return ([category containsString:@"DualShock"] || [category containsString:@"DualSense"]) ? 1 : 0;
}

// YES when an alias now holds another skin
+ (BOOL)installPlatformSkinAliasIfNeeded {
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (!docs) return NO;
    NSFileManager* fm = NSFileManager.defaultManager;
    NSString* (^source)(NSString*) = ^NSString*(NSString* alias) {
        NSString* marker = [[docs stringByAppendingPathComponent:alias] stringByAppendingString:@".source"];
        return [NSString stringWithContentsOfFile:marker encoding:NSUTF8StringEncoding error:nil];
    };
    // With no pad yet the skins stay the ones the last pad had
    const int pad = MCLEPadIsPlayStation();
    const BOOL playStation = pad < 0 ? [source(@"platformskinHD.swf") isEqualToString:@"skinHDOrbis.swf"] : pad == 1;
    BOOL changed = NO;
    struct AliasPair { NSString* alias; NSArray<NSString*>* sources; };
    AliasPair pairs[] = {
        { @"platformskinHD.swf", playStation
              ? @[ @"skinHDOrbis.swf", @"skinHDWin.swf", @"skinHDDurango.swf", @"skinHDWiiU.swf" ]
              : @[ @"skinHDWin.swf", @"skinHDDurango.swf", @"skinHDOrbis.swf", @"skinHDWiiU.swf" ] },
        { @"platformskin.swf", playStation
              ? @[ @"skinOrbis.swf", @"skinWin.swf", @"skinPS3.swf", @"skinVita.swf" ]
              : @[ @"skinWin.swf", @"skinPS3.swf", @"skinVita.swf" ] },
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        NSString* aliasPath = [docs stringByAppendingPathComponent:pairs[i].alias];
        // Which skin the alias holds, kept next to it
        NSString* markerPath = [aliasPath stringByAppendingString:@".source"];
        NSString* current = [NSString stringWithContentsOfFile:markerPath encoding:NSUTF8StringEncoding error:nil];
        for (NSString* candidate in pairs[i].sources) {
            NSString* srcPath = [docs stringByAppendingPathComponent:candidate];
            if (![fm fileExistsAtPath:srcPath]) continue;
            // An alias from before the marker existed came from the first
            // Windows skin
            if (!current && [fm fileExistsAtPath:aliasPath] && ![candidate containsString:@"Orbis"]) {
                [candidate writeToFile:markerPath atomically:YES encoding:NSUTF8StringEncoding error:nil];
                break;
            }
            if ([current isEqualToString:candidate] && [fm fileExistsAtPath:aliasPath]) break;
            [fm removeItemAtPath:aliasPath error:nil];
            NSError* err = nil;
            if ([fm copyItemAtPath:srcPath toPath:aliasPath error:&err]) {
                [candidate writeToFile:markerPath atomically:YES encoding:NSUTF8StringEncoding error:nil];
                NSLog(@"[platformskin] aliased %@ -> %@", candidate, pairs[i].alias);
                changed = YES;
            } else {
                NSLog(@"[platformskin] copy %@ -> %@ failed: %@", candidate, pairs[i].alias, err);
            }
            break;
        }
    }
    g_platform_skin_orbis = [source(@"platformskinHD.swf") isEqualToString:@"skinHDOrbis.swf"];
    return changed;
}

// A pad connected or went. When the skins change with it, they load again
// like UIController::ReloadSkin: the libraries, then the movies made with
// the old ones that show the pad's buttons.
- (void)platformPadChanged {
    if (![MinecraftViewController installPlatformSkinAliasIfNeeded]) return;
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    MCLEReloadSkinLibraries(true);
    if (self.scenerAttached) [self reloadTooltipsComponent];
    // ReloadAll for the scenes that draw the pad
    if (!g_pending_dialog && ([self.currentMenuSwf isEqualToString:@"Controls1080.swf"] ||
                              [self.currentMenuSwf isEqualToString:@"HowToPlay1080.swf"])) {
        [self transitionToMenuNamed:self.currentMenuSwf];
    }
}

// The tooltips component's movie up again on the new platform skin, its
// glyphs are the skin's
- (void)reloadTooltipsComponent {
    extern PlayerHandle* g_ruffle_player;
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    NSString* path = [docs stringByAppendingPathComponent:@"ToolTips1080.swf"];
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!data.length) return;
    NSString* url = [NSString stringWithFormat:@"file://%@",
        [path stringByReplacingOccurrencesOfString:@" " withString:@"%20"]];
    ruffle_ios_remove_sibling_at_depth(g_ruffle_player, kTooltipsDepth);
    int rc = ruffle_ios_add_sibling_swf_to_root(g_ruffle_player,
        (const uint8_t*)data.bytes, data.length,
        (const uint8_t*)url.UTF8String, strlen(url.UTF8String),
        kTooltipsDepth, 1.0f, 1.0f, 0.0f, 0.0f);
    NSLog(@"[platformskin] tooltips movie reloaded -> %d", rc);
    if (rc != 1) return;
    // A few frames so its init builds the strip, as attachMenuScenery does
    for (int i = 0; i < 10; ++i) {
        ruffle_ios_player_tick_headless_preserve_xui(g_ruffle_player, 1.0f / 60.0f);
    }
    ruffle_ios_set_sibling_takes_input(g_ruffle_player, kTooltipsDepth, 0);
    MCLETooltipsMovieReloaded();
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;

    // Print every file the app's sandbox Documents folder actually
    // contains. This is the only ground truth for "did MainMenu1080.swf
    // make it into the right container" - independent of how 3uTools /
    // AltStore / Sideloadly chose to copy it in.
    {
        NSFileManager* fm = NSFileManager.defaultManager;
        NSURL* docs = [fm URLsForDirectory:NSDocumentDirectory
                                 inDomains:NSUserDomainMask].firstObject;
        NSLog(@"[MCLE/docs] sandbox Documents = %@", docs.path);
        NSArray<NSString*>* entries = [fm contentsOfDirectoryAtPath:docs.path error:nil];
        NSLog(@"[MCLE/docs] %lu entries", (unsigned long)entries.count);
        for (NSString* e in entries) {
            NSString* full = [docs.path stringByAppendingPathComponent:e];
            BOOL isDir = NO;
            [fm fileExistsAtPath:full isDirectory:&isDir];
            NSDictionary* attrs = [fm attributesOfItemAtPath:full error:nil];
            unsigned long long sz = isDir ? 0 : [attrs[NSFileSize] unsignedLongLongValue];
            NSLog(@"[MCLE/docs]   %@%@ (%llu bytes)",
                  e, isDir ? @"/" : @"", sz);
        }
    }

    // Minecraft::main's part before the menus: the statics, and the default
    // game rules with the tutorial's level generator
    mcle_minecraft_main();

    // Lay down the platform-skin alias before any SWF parses
    // imports. Without this, ToolTips1080.swf (and any other
    // 1080p scene that imports platformskinHD.swf) 404s on the
    // imports for controller-button glyphs and the icons stay
    // blank.
    [MinecraftViewController installPlatformSkinAliasIfNeeded];
    // The pads GameController reports after launch, and any connected
    // later, get their own skin too
    __weak MinecraftViewController* weakSelf = self;
    for (NSNotificationName name in @[ GCControllerDidConnectNotification, GCControllerDidDisconnectNotification ]) {
        [NSNotificationCenter.defaultCenter addObserverForName:name object:nil queue:NSOperationQueue.mainQueue
                                                    usingBlock:^(NSNotification* _Nonnull) {
            [weakSelf platformPadChanged];
        }];
    }

    CGRect frame = self.view.bounds;

    // Metal-backed view fills the whole screen. Renderer draws into its layer.
    self.metalView = [[MetalView alloc] initWithFrame:frame];
    self.metalView.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.metalView.opaque = YES;
    self.metalView.backgroundColor = UIColor.blackColor;
    [self.view addSubview:self.metalView];

    // SWF layer on top. Iggy composites the UI over the game the same way:
    // the HUD and pause menu draw over the live world.
    self.overlayView = [[MetalView alloc] initWithFrame:frame];
    self.overlayView.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.overlayView.opaque = NO;
    self.overlayView.backgroundColor = UIColor.clearColor;
    self.overlayView.layer.opaque = NO;
    self.overlayView.userInteractionEnabled = NO;
    [self.view addSubview:self.overlayView];

    // Iggy draws items into slots in the middle of drawing the movie. Here
    // they go on a layer of their own above it.
    self.customDrawView = [[MetalView alloc] initWithFrame:frame];
    self.customDrawView.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.customDrawView.opaque = NO;
    self.customDrawView.backgroundColor = UIColor.clearColor;
    self.customDrawView.layer.opaque = NO;
    self.customDrawView.userInteractionEnabled = NO;
    [self.view addSubview:self.customDrawView];

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
    self.statusLabel.hidden = YES;
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
    self.overlayView.layer.contentsScale = scale;
    self.customDrawView.layer.contentsScale = scale;

    // Try to stand up a Ruffle wgpu player that owns this CAMetalLayer.
    // If it succeeds, every frame is driven by Ruffle. If it fails, we
    // fall back to our legacy Metal triangle path for diagnostics.
    extern PlayerHandle* g_ruffle_player;
    extern int g_ruffle_surface_probe;

    g_ruffle_surface_probe =
        ruffle_ios_surface_probe((__bridge void*)self.overlayView.layer);

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
                // UIController::SetupFont registers Mojangles11 next to
                // Mojangles7 (the End poem and the small labels use it).
                // Without its own file it goes to the 7 one, the way the
                // TTF fonts take both names with IggyFontSetIndirectUTF8.
                NSData* font11Data = nil;
                for (NSString* subdir in fontSubdirs) {
                    NSString* dir = subdir.length
                        ? [docsPath stringByAppendingPathComponent:subdir]
                        : docsPath;
                    for (NSString* name in @[@"Mojangles11.ttf", @"Mojang Font_11.ttf"]) {
                        NSString* p = [dir stringByAppendingPathComponent:name];
                        if ([fm fileExistsAtPath:p]) font11Data = [NSData dataWithContentsOfFile:p];
                        if (font11Data.length) break;
                    }
                    if (font11Data.length) break;
                }
                if (!font11Data.length) font11Data = fontData;
                const char* name11 = "Mojangles11";
                rc = ruffle_ios_stage_device_font(
                    (const uint8_t*)name11, strlen(name11),
                    (const uint8_t*)font11Data.bytes, font11Data.length,
                    0, 0);
                NSLog(@"[MinecraftVC] staged Mojangles11 (%luk) rc=%d",
                      (unsigned long)(font11Data.length / 1024), rc);
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
                (__bridge void*)self.overlayView.layer,
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
            }
            if (g_ruffle_player && [self.currentMenuSwf isEqualToString:@"Intro1080.swf"]) {
                [self initIntro];
            } else if (g_ruffle_player) {
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

    // The world renders into metalView's layer. Ruffle has the overlay
    // layer above it, so both can draw every frame.
    mcle_render_init((__bridge void*)self.metalView.layer, pw, ph);
    mcle_metal_attach_ui_layer((__bridge void*)self.customDrawView.layer, pw, ph);

    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];

    // No GPU work while inactive. Submitting from the background leaves
    // the layer black when the app comes back.
    __weak MinecraftViewController* weakSelf = self;
    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    [nc addObserverForName:UIApplicationWillResignActiveNotification object:nil
                     queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
        weakSelf.displayLink.paused = YES;
    }];
    [nc addObserverForName:UIApplicationDidBecomeActiveNotification object:nil
                     queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
        UIApplication.sharedApplication.idleTimerDisabled = YES;
        weakSelf.displayLink.paused = NO;
    }];
    // Launched while locked or in the background: same rule, wait for active.
    if (UIApplication.sharedApplication.applicationState != UIApplicationStateActive)
        self.displayLink.paused = YES;

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
    // How the scenes' UIControl_Base::getLabel reads the movie
    mcle_container_set_label_reader(MCLELabelAt);

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
    mcle_metal_attach_ui_layer(NULL, (int)(sz.width * scale), (int)(sz.height * scale));
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
            // Depth stack documented next to the kFooDepth
            // constants near the top of this file. Order is
            // back-to-front: Panorama, scene root (added later by
            // replace_swf), Logo, dim, dialog (only when up),
            // Tooltips.
            @[@"Panorama1080.swf",     @(-2),                   @(1.5f), @(1.5f), @(-208.6f), @(0.0f)],
            @[@"ComponentLogo1080.swf",@(kLogoDepth),           @(1.0f), @(1.0f), @(0.0f),    @(0.0f)],
            // MenuBackground is the dim backdrop console adds via
            // UIComponent_MenuBackground while a MessageBox dialog
            // is up (UIScene_MessageBox.cpp:49 addComponent).
            // Authored 1080-wide; without stretching to match the
            // Panorama scale it only covered the centre band of
            // the screen. Same 1.5x and pre-shift Panorama uses so
            // it fills iPhone aspect ratios end-to-end.
            @[@"MenuBackground1080.swf",@(kMenuBackgroundDepth),@(1.5f), @(1.5f), @(-208.6f), @(0.0f)],
            @[@"MenuBackground1080.swf",@(kDimDepth),           @(1.5f), @(1.5f), @(-208.6f), @(0.0f)],
            @[@"ToolTips1080.swf",     @(kTooltipsDepth),       @(1.0f), @(1.0f), @(0.0f),    @(0.0f)],
            // UIGroup's tutorial popup, hidden until the tutorial shows it
            @[@"TutorialPopup1080.swf", @(kTutorialPopupDepth), @(1.0f), @(1.0f), @(0.0f),    @(0.0f)],
            // The fullscreen group's PressStartToPlay, with the autosave countdown
            @[@"PressStartToPlay1080.swf", @(kPressStartToPlayDepth), @(1.0f), @(1.0f), @(0.0f), @(0.0f)],
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
            g_ruffle_player, kDimDepth, 0);
        ruffle_ios_set_xui_sibling_visible_at_depth(
            g_ruffle_player, kMenuBackgroundDepth, 0);
        // UILayer adds the tutorial popup hidden
        ruffle_ios_set_xui_sibling_visible_at_depth(
            g_ruffle_player, kTutorialPopupDepth, 0);
        // UILayer::handleInput: components can't take input or focus.
        // The tooltips only get the presses AnimateKeyPress sends them.
        const int components[] = { -2, kLogoDepth, kMenuBackgroundDepth, kDimDepth,
                                   kTooltipsDepth, kTutorialPopupDepth, kPressStartToPlayDepth };
        for (int depth : components) ruffle_ios_set_sibling_takes_input(g_ruffle_player, depth, 0);

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

        // UIComponent_TutorialPopup's constructor, now its movie is up
        mcle_tutorial_popup_movie_ready();
        MCLETutorialPopupApplyOps();
        MCLEPressStartToPlayInit();
    }
}

// Configure the tooltips strip at the bottom of the screen for
// the current scene. Drives ToolTips1080.swf (attached as a
// stage sibling at kTooltipsDepth) via its public AS3 API:
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
    // UIScene_Keyboard has no updateComponents, the logo stays as the scene
    // under it had it
    if ([swfName isEqualToString:@"Keyboard1080.swf"]) return;
    NSSet<NSString*>* logoOff = [NSSet setWithArray:@[
        @"SkinSelectMenu1080.swf",
        @"LeaderboardMenu1080.swf",
        @"LeaderboardsMenu1080.swf",
        @"CreateWorldMenu1080.swf",
        @"LaunchMoreOptionsMenu1080.swf",
    ]];
    int visible = [logoOff containsObject:swfName] ? 0 : 1;
    ruffle_ios_set_xui_sibling_visible_at_depth(
        g_ruffle_player, kLogoDepth, visible);
}

- (void)seedTooltipsForScene:(NSString*)swfName hidden:(BOOL)hidden {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    // UIComponent_Tooltips::updateSafeZone, fullscreen
    const char* setSafe = "SetSafeZone";
    double safe[4] = { kSafeZoneTop, kSafeZoneBottom, kSafeZoneLeft, kSafeZoneRight }; // top, bottom, left, right
    ruffle_ios_call_method_on_sibling_root(g_ruffle_player, kTooltipsDepth,
        (const uint8_t*)setSafe, strlen(setSafe),
        safe, 4);
    // UIComponent_Tooltips::tick's opacity for this scene, 15% at least
    // with a menu up
    MCLETooltipsApplyOpacity(![swfName isEqualToString:@"HUD1080.swf"], 0, true);

    // In game Minecraft::tick sets them, see MCLEGameTooltipsPump
    if ([swfName isEqualToString:@"HUD1080.swf"]) return;

    // Each scene's UIScene::updateTooltips. Most are
    // SetTooltips(pad, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK).
    int ids[11] = { IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    if ([swfName isEqualToString:@"MainMenu1080.swf"]) {
        // UIScene_MainMenu: Select only, there's nothing to back out to
        ids[1] = -1;
    } else if ([swfName isEqualToString:@"SaveMessage1080.swf"]) {
        // UIScene_SaveMessage::updateTooltips: Select only
        ids[1] = -1;
    } else if ([swfName isEqualToString:@"MessageBox1080.swf"]) {
        // UIScene_MessageBox: the dialog cancels
        ids[1] = IDS_TOOLTIPS_CANCEL;
    } else if ([swfName isEqualToString:@"DeathMenu1080.swf"]) {
        // UIScene_DeathMenu: Select only, B does nothing there
        ids[1] = -1;
    } else if ([swfName isEqualToString:@"PauseMenu1080.swf"]) {
        // UIScene_PauseMenu::updateTooltips: the primary host of a full
        // game with saving on gets Change Storage Device on X (which
        // SetTooltips takes off again), sharing is never allowed on
        // Windows64
        ids[2] = IDS_TOOLTIPS_CHANGEDEVICE;
    } else if ([swfName isEqualToString:@"LeaderboardMenu1080.swf"]) {
        // UIScene_LeaderboardsMenu::updateTooltips with no entries
        ids[0] = -1;
        ids[2] = IDS_TOOLTIPS_CHANGE_FILTER;
    } else if ([swfName isEqualToString:@"Credits1080.swf"]) {
        // UIScene_Credits: Back only
        ids[0] = -1;
    } else if ([swfName isEqualToString:@"EndPoem1080.swf"]) {
        // UIScene_EndPoem::updateTooltips
        ids[0] = -1;
        ids[1] = g_endpoem_ignore_input ? -1 : IDS_TOOLTIPS_CONTINUE;
    } else if ([swfName isEqualToString:@"HowToPlay1080.swf"]) {
        // UIScene_HowToPlay::StartPage set them for its page
        return;
    } else if ([swfName isEqualToString:@"SkinSelectMenu1080.swf"]) {
        // UIScene_SkinSelectMenu::updateTooltips set them
        return;
    } else if ([swfName isEqualToString:@"LaunchMoreOptionsMenu1080.swf"]) {
        MCLEMoreOptionsTooltips();
        return;
    } else if ([swfName isEqualToString:@"LoadOrJoinMenu1080.swf"]) {
        MCLELoadOrJoinTooltips();
        return;
    }
    MCLESetTooltips(ids);
}

- (void)initMainMenuButtons {
    NSArray<NSDictionary*>* cfg = @[
        // UIScene_MainMenu constructor, the full version's
        // IDS_DOWNLOADABLECONTENT
        @{ @"name": @"Button1", @"label": MCLEStr(IDS_PLAY_GAME),           @"id": @(0) },
        @{ @"name": @"Button2", @"label": MCLEStr(IDS_LEADERBOARDS),        @"id": @(1) },
        @{ @"name": @"Button3", @"label": MCLEStr(IDS_ACHIEVEMENTS),        @"id": @(2) },
        @{ @"name": @"Button4", @"label": MCLEStr(IDS_HELP_AND_OPTIONS),    @"id": @(3) },
        @{ @"name": @"Button5", @"label": MCLEStr(IDS_DOWNLOADABLECONTENT), @"id": @(4) },
        @{ @"name": @"Button6", @"label": MCLEStr(IDS_EXIT_GAME),           @"id": @(5) },
    ];
    [self attachMenuScenery];
    [self applyMenuButtonConfig:cfg];
    // UIScene_MainMenu::handleGainFocus
    MCLESetPathVisible(@"Timer", NO);
}

// UIScene_Intro constructor. No panorama, logo or tooltips with it, the
// movie plays the logos for the platform and calls handleAnimationEnd.
- (void)initIntro {
    g_intro_ignore_navigate = false;
    self.menuButtonConfig = nil;
    self.splashLabel.hidden = YES;
    if (!self.introBackdrop) {
        self.introBackdrop = [[UIView alloc] initWithFrame:self.view.bounds];
        self.introBackdrop.autoresizingMask =
            UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        self.introBackdrop.backgroundColor = UIColor.whiteColor;
        self.introBackdrop.userInteractionEnabled = NO;
        [self.view insertSubview:self.introBackdrop aboveSubview:self.metalView];
    }
    // Windows64's platformIdx 0, no ESRB skip, not China
    MCLECallPath(@"", @"SetIntroPlatform", @[@0, @NO, @NO]);
}

// UIScene_Intro::handleInput (A) and handleAnimationEnd: once, on to the
// save message
- (void)introNavigate {
    if (g_intro_ignore_navigate || ![self.currentMenuSwf isEqualToString:@"Intro1080.swf"]) return;
    g_intro_ignore_navigate = true;
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    BOOL haveSaveMessage = [NSFileManager.defaultManager fileExistsAtPath:
        [docs stringByAppendingPathComponent:@"SaveMessage1080.swf"]];
    if (haveSaveMessage) [self transitionToMenuNamed:@"SaveMessage1080.swf"];
    else [self navigateToHomeMenu];
}

// UIScene_SaveMessage constructor
- (void)initSaveMessage {
    // addComponent eUIComponent_Panorama and eUIComponent_Logo
    [self attachMenuScenery];
    [self applyMenuButtonConfig:@[
        @{ @"name": @"Confirm", @"label": MCLEStr(IDS_CONFIRM_OK), @"id": @(0) },
    ]];
    MCLECallPath(@"Description", @"Init", @[MCLEStr(IDS_SAVE_ICON_MESSAGE)]);
    // Russian needs to resize the box
    MCLECallPath(@"", @"AutoResize", @[]);
    // If we have a signed in user connected, let's get the DLC now
    mcle_dlc_install_if_needed(0);
}

// UIController::NavigateToHomeMenu: the menus go back on the default
// texture pack, then the main menu with nothing to go back to
- (void)navigateToHomeMenu {
    self.menuStack = nil;
    mcle_texture_packs_navigate_to_home_menu();
    mcle_handle_xui_actions();
    if (mcle_ui_take_skin_reload()) MCLEReloadSkinLibraries();
    [self transitionToMenuNamed:@"MainMenu1080.swf"];
}

- (void)initHelpAndOptionsButtons {
    // Mirrors UIScene_HelpAndOptionsMenu constructor on console: only the
    // five universally-visible buttons are configured (Reinstall and
    // Debug are runtime-removed in the console release build). BUTTON_HAO_*
    // ids match the enum on console.
    NSArray<NSDictionary*>* cfg = @[
        @{ @"name": @"Button1", @"label": MCLEStr(IDS_CHANGE_SKIN),  @"id": @(0) },
        @{ @"name": @"Button2", @"label": MCLEStr(IDS_HOW_TO_PLAY),  @"id": @(1) },
        @{ @"name": @"Button3", @"label": MCLEStr(IDS_CONTROLS),     @"id": @(2) },
        @{ @"name": @"Button4", @"label": MCLEStr(IDS_SETTINGS),     @"id": @(3) },
        @{ @"name": @"Button5", @"label": MCLEStr(IDS_CREDITS),      @"id": @(4) },
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
    // Labels match the IDS_ constants in stringsGeneric.xml:
    //   IDS_VIEW_BOBBING             "View Bobbing"
    //   IDS_HINTS                    "Hints"
    //   IDS_IN_GAME_TOOLTIPS         "In-Game Tooltips"
    //   IDS_IN_GAME_GAMERTAGS        "In-Game Gamertags"
    //   IDS_UNHIDE_MASHUP_WORLDS     "Show all Mash-up Worlds"
    Cb checkboxes[] = {
        {"ViewBob",         MCLEStr(IDS_VIEW_BOBBING).UTF8String,      0, (BOOL)mcle_settings_get(MCLE_SETTING_ViewBob)},
        {"ShowHints",       MCLEStr(IDS_HINTS).UTF8String,             1, (BOOL)mcle_settings_get(MCLE_SETTING_Hints)},
        {"ShowTooltips",    MCLEStr(IDS_IN_GAME_TOOLTIPS).UTF8String,  2, (BOOL)mcle_settings_get(MCLE_SETTING_Tooltips)},
        {"InGameGamertags", MCLEStr(IDS_IN_GAME_GAMERTAGS).UTF8String, 3, (BOOL)mcle_settings_get(MCLE_SETTING_GamertagsVisible)},
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
    int diffClamped = MAX(0, MIN(difficulty, 3));
    NSString* autosaveLabel = MCLESettingsSliderLabel(@"SettingsOptionsMenu1080.swf", 5, autosave);
    NSString* difficultyLabel = MCLESettingsSliderLabel(@"SettingsOptionsMenu1080.swf", 7, diffClamped);

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

    // The difficulty description under the slider
    MCLECallPath(@"DifficultyText", @"Init", @[MCLEDifficultyText(diffClamped)]);

    // UIScene::removeControl for what this build doesn't have: no mash-up
    // packs to unhide, and no difficulty change once in game
    MCLECallPath(@"", @"RemoveObject", @[@"ShowMashUpWorlds", @YES]);
    if (mcle_world_is_ticking()) {
        MCLESetPathVisible(@"DifficultyText", false);
        MCLECallPath(@"", @"RemoveObject", @[@"Difficulty", @YES]);
    }
    // No language select either (_ENABLE_LANGUAGE_SELECT isn't defined), so
    // its button is Longer Loading Screens, in and out of game
    MCLECallPath(@"Languages", @"Init", @[MCLELongerLoadingLabel(), @(kOptionsLanguagesControl)]);

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
           @"label": MCLESettingsSliderLabel(@"SettingsAudioMenu1080.swf", 0, music),
           @"id":    @(0), @"min": @(0), @"max": @(100), @"cur": @(music) },
        @{ @"name":  @"Sound",
           @"label": MCLESettingsSliderLabel(@"SettingsAudioMenu1080.swf", 1, sound),
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
    // Label format "%ls: %d%%" from UIScene_SettingsControlMenu.cpp
    // lines 103 / 111 with IDS_SLIDER_SENSITIVITY_INGAME =
    // "Game Sensitivity" and IDS_SLIDER_SENSITIVITY_INMENU =
    // "Interface Sensitivity".
    NSArray<NSDictionary*>* sliders = @[
        @{ @"name":  @"SensitivityInGame",
           @"label": MCLESettingsSliderLabel(@"SettingsControlMenu1080.swf", 0, ingame),
           @"id":    @(0), @"min": @(0), @"max": @(200), @"cur": @(ingame) },
        @{ @"name":  @"SensitivityInMenu",
           @"label": MCLESettingsSliderLabel(@"SettingsControlMenu1080.swf", 1, inmenu),
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

    // Labels from IDS_CHECKBOX_RENDER_CLOUDS / RENDER_BEDROCKFOG /
    // CUSTOM_SKIN_ANIM in stringsGeneric.xml.
    struct Cb { const char* name; const char* label; int id; BOOL checked; };
    Cb checkboxes[] = {
        {"Clouds",         mcle_string(IDS_CHECKBOX_RENDER_CLOUDS),    0, (BOOL)mcle_settings_get(MCLE_SETTING_Clouds)},
        {"BedrockFog",     mcle_string(IDS_CHECKBOX_RENDER_BEDROCKFOG), 1, (BOOL)mcle_settings_get(MCLE_SETTING_BedrockFog)},
        {"CustomSkinAnim", mcle_string(IDS_CHECKBOX_CUSTOM_SKIN_ANIM),  2, (BOOL)mcle_settings_get(MCLE_SETTING_CustomSkinAnim)},
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
           @"label": MCLESettingsSliderLabel(@"SettingsGraphicsMenu1080.swf", 3, renderLevel),
           @"id":    @(3), @"min": @(0), @"max": @(5), @"cur": @(renderLevel) },
        @{ @"name":  @"Gamma",
           @"label": MCLESettingsSliderLabel(@"SettingsGraphicsMenu1080.swf", 4, gamma),
           @"id":    @(4), @"min": @(0), @"max": @(100), @"cur": @(gamma) },
        @{ @"name":  @"FOV",
           @"label": MCLESettingsSliderLabel(@"SettingsGraphicsMenu1080.swf", 5, fovSlider),
           @"id":    @(5), @"min": @(0), @"max": @(100), @"cur": @(fovSlider) },
        @{ @"name":  @"InterfaceOpacity",
           @"label": MCLESettingsSliderLabel(@"SettingsGraphicsMenu1080.swf", 6, opacity),
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

    // Labels from IDS_CHECKBOX_DISPLAY_HUD / DISPLAY_HAND /
    // DEATH_MESSAGES / ANIMATED_CHARACTER /
    // VERTICAL_SPLIT_SCREEN / DISPLAY_SPLITSCREENGAMERTAGS in
    // stringsGeneric.xml. Note that the last four don't have the
    // "Display" prefix that earlier versions of this port assumed.
    struct Cb { const char* name; const char* label; int id; BOOL checked; };
    Cb checkboxes[] = {
        {"DisplayHUD",               mcle_string(IDS_CHECKBOX_DISPLAY_HUD),                 0, (BOOL)mcle_settings_get(MCLE_SETTING_DisplayHUD)},
        {"DisplayHand",              mcle_string(IDS_CHECKBOX_DISPLAY_HAND),                1, (BOOL)mcle_settings_get(MCLE_SETTING_DisplayHand)},
        {"DisplayDeathMessages",     mcle_string(IDS_CHECKBOX_DEATH_MESSAGES),              2, (BOOL)mcle_settings_get(MCLE_SETTING_DeathMessages)},
        {"DisplayAnimatedCharacter", mcle_string(IDS_CHECKBOX_ANIMATED_CHARACTER),          3, (BOOL)mcle_settings_get(MCLE_SETTING_AnimatedCharacter)},
        {"Splitscreen",              mcle_string(IDS_CHECKBOX_VERTICAL_SPLIT_SCREEN),       4, (BOOL)mcle_settings_get(MCLE_SETTING_SplitScreenVertical)},
        {"ShowSplitscreenGamertags", mcle_string(IDS_CHECKBOX_DISPLAY_SPLITSCREENGAMERTAGS), 5, (BOOL)mcle_settings_get(MCLE_SETTING_DisplaySplitscreenGamertags)},
    };
    for (auto& c : checkboxes) {
        ruffle_ios_call_init_checkbox(
            g_ruffle_player,
            (const uint8_t*)c.name,  strlen(c.name),
            (const uint8_t*)c.label, strlen(c.label),
            (double)c.id,
            c.checked ? 1 : 0);
    }

    // UIScene_SettingsUIMenu: the sliders show the setting plus one
    int uiSize = MIN((int)mcle_settings_get(MCLE_SETTING_UISize), 2) + 1;
    int uiSizeSplit = MIN((int)mcle_settings_get(MCLE_SETTING_UISizeSplitscreen), 2) + 1;
    // IDS_SLIDER_UISIZE = "HUD Size",
    // IDS_SLIDER_UISIZESPLITSCREEN = "HUD Size (Splitscreen)".
    NSArray<NSDictionary*>* sliders = @[
        @{ @"name":  @"UISize",
           @"label": MCLESettingsSliderLabel(@"SettingsUIMenu1080.swf", 6, uiSize),
           @"id":    @(6), @"min": @(1), @"max": @(3), @"cur": @(uiSize) },
        @{ @"name":  @"UISizeSplitscreen",
           @"label": MCLESettingsSliderLabel(@"SettingsUIMenu1080.swf", 7, uiSizeSplit),
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

// UIScene_LoadOrJoinMenu::AddDefaultButtons, then the saves its tick adds
// once GetSaveInfo has them, onto the cleared SavesList
- (void)loadOrJoinAddSaves {
    extern PlayerHandle* g_ruffle_player;
    const char* saves = "SavesList";

    // Default first item: "Create New World". Console adds this
    // at JOIN_LOAD_CREATE_BUTTON_INDEX=0 regardless of whether
    // any saves exist (UIScene_LoadOrJoinMenu.cpp:1098).
    const char* createLabel = "Create New World";
    ruffle_ios_call_list_add_item(g_ruffle_player,
        (const uint8_t*)saves, strlen(saves),
        (const uint8_t*)createLabel, strlen(createLabel),
        0.0,
        (const uint8_t*)"", 0);
    g_load_default_buttons = 1;

    // AddDefaultButtons: then the level generators, the tutorial first
    int generatorCount = mcle_level_generators_refresh();
    for (int i = 0; i < generatorCount; ++i) {
        const char* name = mcle_level_generator_name(i);
        ruffle_ios_call_list_add_item(g_ruffle_player,
            (const uint8_t*)saves, strlen(saves),
            (const uint8_t*)name, strlen(name),
            (double)g_load_default_buttons,
            (const uint8_t*)"", 0);
        ++g_load_default_buttons;
    }

    // Saves after the default buttons, newest first, item data is the
    // running index (UIControl_SaveList::addItem passes m_itemCount).
    int saveCount = mcle_saves_refresh();
    for (int i = 0; i < saveCount; ++i) {
        const char* name = mcle_saves_name(i);
        ruffle_ios_call_list_add_item(g_ruffle_player,
            (const uint8_t*)saves, strlen(saves),
            (const uint8_t*)name, strlen(name),
            (double)(i + g_load_default_buttons),
            (const uint8_t*)"", 0);
    }

    // Thumbnails, keyed by save folder like the Win64 tick() does
    for (int i = 0; i < saveCount; ++i) {
        NSString* thumb = [NSString stringWithUTF8String:mcle_saves_thumbnail(i)];
        NSData* png = thumb.length ? [NSData dataWithContentsOfFile:thumb] : nil;
        const char* tex = mcle_saves_folder(i);
        if (png.length) {
            ruffle_ios_register_xui_bitmap(g_ruffle_player,
                (const uint8_t*)tex, strlen(tex),
                (const uint8_t*)png.bytes, png.length, 1.0f, 1.0f);
        }
        MCLECallPath(@"SavesList", @"SetTextureName",
                     @[@(i + g_load_default_buttons), [NSString stringWithUTF8String:tex]]);
    }
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

    // Initialise starts on the first item, the focus is wherever SetFocus
    // below puts it, and input is back on by the end like the constructor
    // and handleGainFocus
    g_load_save_list_index = 0;
    g_load_focus_control = -1;
    g_load_or_join_ignore_input = false;

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
        { "SavesListTitle", MCLEStr(IDS_START_GAME).UTF8String },
        { "JoinListTitle",  MCLEStr(IDS_JOIN_GAME).UTF8String },
        { "NoGames",        MCLEStr(IDS_NO_GAMES_FOUND).UTF8String },
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

    [self loadOrJoinAddSaves];

    // m_spaceIndicatorSaves: init(L"", eControl_SpaceIndicator, 0, MAXIMUM_SAVE_STORAGE)
    // then addSave per save, with the focused save highlighted.
    MCLECallPath(@"SaveSizeBar", @"Init", @[@""]);
    MCLESelectSaveSize(-1);

    // Auto-focus via SWF's SetFocus(-1). SavesList has tabIndex=1
    // per LoadOrJoinMenu.as __setTab_SavesList_... so FJ_Document.
    // SetFocus picks it.
    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// The message box was on the same stage and took its focus. The scene's own
// movie keeps it on Windows64, so it goes back to the save it was on.
static void MCLELoadOrJoinRefocusSave(void) {
    MCLECallPath(@"SavesList", @"HighlightItem", @[@(g_load_save_list_index)]);
}

// UIScene_LoadOrJoinMenu::KeyboardCompleteWorldNameCallback. The keyboard
// has gone back, and the list's movie reloads after it, which is the
// e_SavesRepopulate RenameSaveDataReturned asks for.
static int MCLELoadOrJoinKeyboardComplete(void* lpParam, const bool bRes) {
    g_load_or_join_ignore_input = false;
    if (bRes) {
        // Win64_GetKeyboardText
        const std::wstring& text = mcle_keyboard_text();
        // check the name is valid
        if (!text.empty()) {
            int listPos = g_load_save_list_index - g_load_default_buttons;
            mcle_saves_rename(listPos, MCLEStringFromWide(text).UTF8String);
        }
    }
    return 0;
}

// UIScene_LoadOrJoinMenu::handleInput's ACTION_MENU_RIGHT_SCROLL, and the
// ACTION_MENU_X Windows64 adds for it. Saving is never disabled and there's
// always space for a save, so it's Save Options rather than Delete Save.
- (void)loadOrJoinSaveOptions {
    if (g_load_or_join_ignore_input) return;
    // 4J-PB - check we are on a valid save
    if (!MCLELoadOrJoinSavesListHasFocus() || g_load_default_buttons == 0 ||
        g_load_save_list_index < g_load_default_buttons) return;
    g_load_or_join_ignore_input = true;
    __weak MinecraftViewController* weakSelf = self;
    [self presentDialogWithTitle:MCLEStr(IDS_TOOLTIPS_SAVEOPTIONS)
                         content:MCLEStr(IDS_TEXT_SAVEOPTIONS)
                         buttons:@[MCLEStr(IDS_CONFIRM_CANCEL), MCLEStr(IDS_TITLE_RENAMESAVE),
                                   MCLEStr(IDS_TOOLTIPS_DELETESAVE)]
                      focusIndex:0
                        callback:^(MCLEDialogResult r) {
        [weakSelf loadOrJoinSaveOptionsReturned:r];
    }];
    mcle_audio_play_ui_sfx("press", 1.0f, 1.0f);
}

// UIScene_LoadOrJoinMenu::SaveOptionsDialogReturned, results switched for
// this dialog: the first button (Cancel) is Accept
- (void)loadOrJoinSaveOptionsReturned:(MCLEDialogResult)result {
    MCLELoadOrJoinRefocusSave();
    if (result == MCLEDialogResultDecline) {
        // rename: Windows64 without the keyboard and mouse brings up
        // UIScene_Keyboard on the save's name
        g_load_or_join_ignore_input = true;
        MCLEKeyboardInitData kbData;
        kbData.title = MCLEWideString(MCLEStr(IDS_RENAME_WORLD_TITLE));
        const char* saveName = mcle_saves_name(g_load_save_list_index - g_load_default_buttons);
        kbData.defaultText = MCLEWideString([NSString stringWithUTF8String:saveName] ?: @"");
        kbData.maxChars = 25;
        kbData.callback = &MCLELoadOrJoinKeyboardComplete;
        [self navigateToKeyboard:kbData];
    } else if (result == MCLEDialogResultThird) {
        // delete the save game
        // Have to ask the player if they are sure they want to delete this game
        __weak MinecraftViewController* weakSelf = self;
        [self presentDialogWithTitle:MCLEStr(IDS_TOOLTIPS_DELETESAVE)
                             content:MCLEStr(IDS_TEXT_DELETE_SAVE)
                             buttons:@[MCLEStr(IDS_CONFIRM_CANCEL), MCLEStr(IDS_CONFIRM_OK)]
                          focusIndex:0
                            callback:^(MCLEDialogResult r) {
            [weakSelf loadOrJoinDeleteSaveDialogReturned:r];
        }];
    } else {
        // reset the tooltips
        MCLELoadOrJoinTooltips();
        g_load_or_join_ignore_input = false;
    }
}

// UIScene_LoadOrJoinMenu::DeleteSaveDialogReturned, results switched: OK
// deletes. Windows64 removes the save's own folder rather than going through
// StorageManager.DeleteSaveData.
- (void)loadOrJoinDeleteSaveDialogReturned:(MCLEDialogResult)result {
    MCLELoadOrJoinRefocusSave();
    // Check that we have a valid save selected (can get a bad index if the save list has been refreshed)
    bool validSelection = g_load_default_buttons != 0 && g_load_save_list_index >= g_load_default_buttons;
    if (result == MCLEDialogResultDecline && validSelection) {
        // Use m_saveDetails (sorted display order) so the correct folder is targeted
        int displayIdx = g_load_save_list_index - g_load_default_buttons;
        bool bSuccess = mcle_saves_delete(displayIdx) != 0;
        [self loadOrJoinDeleteSaveDataReturned:bSuccess];
    } else {
        g_load_or_join_ignore_input = false;
    }
}

// UIScene_LoadOrJoinMenu::DeleteSaveDataReturned, then its tick's
// e_SavesRepopulateAfterDelete: the list is wiped and filled again from disk
- (void)loadOrJoinDeleteSaveDataReturned:(bool)bRes {
    extern PlayerHandle* g_ruffle_player;
    if (bRes) {
        g_load_or_join_ignore_input = false;
        const char* saves = "SavesList";
        ruffle_ios_call_list_remove_all(g_ruffle_player, (const uint8_t*)saves, strlen(saves));
        [self loadOrJoinAddSaves];
        // clearList leaves the selection on the first button, Create New World
        g_load_save_list_index = 0;
        // m_spaceIndicatorSaves.reset(), then addSave for each
        MCLESelectSaveSize(-1);
    } else {
        g_load_or_join_ignore_input = false;
    }
    MCLELoadOrJoinTooltips();
}

static NSString* MCLEDifficultyLabel(int d) {
    static const int kTitles[4] = {
        IDS_DIFFICULTY_TITLE_PEACEFUL, IDS_DIFFICULTY_TITLE_EASY,
        IDS_DIFFICULTY_TITLE_NORMAL, IDS_DIFFICULTY_TITLE_HARD,
    };
    d = MAX(0, MIN(d, 3));
    return [NSString stringWithFormat:@"%@: %@", MCLEStr(IDS_SLIDER_DIFFICULTY), MCLEStr(kTitles[d])];
}

// UIScene::registerSubstitutionTexture, a png the movie shows by name
static void MCLERegisterSubstitutionTexture(NSString* textureName, const unsigned char* pbData, unsigned int dwLength) {
    extern PlayerHandle* g_ruffle_player;
    const char* tex = textureName.UTF8String;
    ruffle_ios_register_xui_bitmap(g_ruffle_player, (const uint8_t*)tex, strlen(tex), pbData, dwLength, 1.0f, 1.0f);
}

static NSString* MCLETexturePackIconName(int index) {
    return [NSString stringWithFormat:@"tpack%08x", mcle_texture_pack_id(index)];
}

// The texture pack list as the CreateWorldMenu and LoadMenu constructors
// fill it: each pack with an icon, the icon registered as tpack<id>
static void MCLEAddTexturePacks(void) {
    int texturePacksCount = mcle_texture_pack_count();
    for (int i = 0; i < texturePacksCount; ++i) {
        unsigned int dwImageBytes = 0;
        const unsigned char* pbImageData = mcle_texture_pack_icon(i, &dwImageBytes);
        if (dwImageBytes > 0 && pbImageData) {
            NSString* imageName = MCLETexturePackIconName(i);
            MCLERegisterSubstitutionTexture(imageName, pbImageData, dwImageBytes);
            MCLECallPath(@"MainPanel.TexturePackSelector", @"addPack", @[@(i), imageName]);
        }
    }
}

// IUIScene_StartGame::UpdateTexturePackDescription. A slot without a pack
// is one from TMS that isn't installed, which upstream hasn't implemented.
static void MCLEUpdateTexturePackDescription(int index) {
    if (index < 0 || index >= mcle_texture_pack_count()) return;
    NSString* name = [NSString stringWithUTF8String:mcle_texture_pack_name(index).c_str()];
    NSString* desc = [NSString stringWithUTF8String:mcle_texture_pack_desc(index).c_str()];
    MCLECallPath(@"TexturePackPanel.TexturePackName", @"SetLabel", @[name ?: @""]);
    MCLECallPath(@"TexturePackPanel.TexturePackDescription", @"SetLabel", @[desc ?: @""]);

    MCLECallPath(@"TexturePackPanel.Icon", @"SetTextureName", @[MCLETexturePackIconName(index)]);

    unsigned int dwImageBytes = 0;
    const unsigned char* pbImageData = mcle_texture_pack_comparison(index, &dwImageBytes);
    if (dwImageBytes > 0 && pbImageData) {
        NSString* imageName = [NSString stringWithFormat:@"texturePackComparison%08x", mcle_texture_pack_id(index)];
        MCLERegisterSubstitutionTexture(imageName, pbImageData, dwImageBytes);
        MCLECallPath(@"TexturePackPanel.ComparisonPic", @"SetTextureName", @[imageName]);
    } else {
        MCLECallPath(@"TexturePackPanel.ComparisonPic", @"SetTextureName", @[@""]);
    }
}

// The list, the current pack's description and its slot selected. The
// movie is made again coming back from More Options, so the description
// panel slides out again if it was out on the one before.
static void MCLEFillTexturePackList(void) {
    MCLEAddTexturePacks();
    MCLEUpdateTexturePackDescription(g_texture_pack_index);
    MCLECallPath(@"MainPanel.TexturePackSelector", @"SelectSlot", @[@(g_texture_pack_index)]);
    if (g_texture_pack_desc_displayed) MCLECallPath(@"", @"SlideLeft", @[]);
}

// IUIScene_StartGame::UpdateCurrentTexturePack. A slot without a pack is
// the upsell, which upstream hasn't implemented.
static void MCLEUpdateCurrentTexturePack(int iSlot) {
    g_texture_pack_index = iSlot;
    if (iSlot >= 0 && iSlot < mcle_texture_pack_count()) {
        g_start_options.dwTexturePack = mcle_texture_pack_id(iSlot);
    }
}

// IUIScene_StartGame::HandleDLCMountingComplete, from the end of the DLC
// mount. The scene keeps its state under More Options, its list is filled
// when it's back.
extern "C" void mcle_start_game_dlc_mounting_complete(void) {
    g_texture_pack_index = mcle_texture_pack_index(0);
    g_texture_pack_waiting_for_dlc = false;
    if (![g_current_scene_name isEqualToString:@"CreateWorldMenu1080.swf"] &&
        ![g_current_scene_name isEqualToString:@"LoadMenu1080.swf"]) {
        return;
    }
    // clear out the current texture pack list
    MCLECallPath(@"MainPanel.TexturePackSelector", @"removeAllItems", @[]);
    MCLEAddTexturePacks();
    MCLEUpdateTexturePackDescription(g_texture_pack_index);
    MCLECallPath(@"MainPanel.TexturePackSelector", @"SelectSlot", @[@(g_texture_pack_index)]);
}

// UIScene_LoadMenu constructor (Win64 save path), plus what its tick()
// pulls from the thumbnail metadata. Control ids follow the scene's
// EControls: GameMode 0, Difficulty 1, MoreOptions 2, LoadWorld 3,
// TexturePackList 4, OnlineGame 5.
- (void)initLoadMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    [self attachMenuScenery];

    int idx = g_load_save_index;
    // Opened from the save list. Back from More Options the scene and what
    // was picked on it are still there.
    if (g_load_menu_fresh) {
        g_load_menu_fresh = false;
        g_load_game_mode = 0;
        g_load_has_been_in_creative = false;
        unsigned int opts = 0;
        bool read = idx >= 0 && idx < mcle_saves_count() && mcle_saves_host_options(idx, &opts);
        if (read) {
            g_load_has_been_in_creative = (opts & 0x200) != 0;   // GAME_HOST_OPTION_BITMASK_BEENINCREATIVE
            g_load_game_mode = (opts & 0x30) >> 4;               // GAME_HOST_OPTION_BITMASK_GAMETYPE
            if (g_load_game_mode != 1) g_load_game_mode = 0;
        }
        g_start_options = MCLELaunchMoreOptions();
        MCLEStartGameOffline(g_start_options);
        mcle_load_menu_options(g_start_options, read, opts, g_load_is_save_owner);

        // IUIScene_StartGame's constructor, then the texture pack the save
        // was played with from the thumbnail like tick() reads it
        g_texture_pack_desc_displayed = false;
        if (idx >= 0 && idx < mcle_saves_count()) g_start_options.dwTexturePack = mcle_saves_texture_pack(idx);
        g_texture_pack_index = mcle_texture_pack_index(g_start_options.dwTexturePack);
        // block input if we're waiting for DLC to install. The end of dlc mounting fills the list.
        g_texture_pack_waiting_for_dlc = mcle_dlc_start_install_process(0);
    }

    MCLECallPath(@"MainPanel.GameName", @"Init", @[MCLEStr(IDS_WORLD_NAME)]);
    MCLECallPath(@"MainPanel.Seed", @"Init", @[@""]);
    MCLECallPath(@"MainPanel.CreatedMode", @"Init", @[MCLEStr(IDS_CREATED_IN_SURVIVAL)]);
    MCLECallPath(@"MainPanel.GameModeToggle", @"Init", @[MCLEStr(IDS_GAMEMODE_SURVIVAL), @0]);
    MCLECallPath(@"MainPanel.MoreOptions", @"Init", @[MCLEStr(IDS_MORE_OPTIONS), @2]);
    MCLECallPath(@"MainPanel.LoadSettings", @"Init", @[MCLEStr(IDS_LOAD), @3]);
    MCLECallPath(@"MainPanel.TexturePackSelector", @"Init", @[MCLEStr(IDS_DLC_MENU_TEXTUREPACKS), @4]);
    MCLECallPath(@"TexturePackPanel.TexturePackName", @"Init", @[@""]);
    MCLECallPath(@"TexturePackPanel.TexturePackDescription", @"Init", @[@""]);

    int diff = MAX(0, MIN((int)mcle_settings_get(MCLE_SETTING_Difficulty), 3));
    MCLECallPath(@"MainPanel.Difficulty", @"Init", @[MCLEDifficultyLabel(diff), @1, @0, @3, @(diff)]);

    // Not signed in to an online service, so the box is off and disabled
    MCLECallPath(@"MainPanel.CheckboxOnline", @"EnableCheckBox", @[@NO]);
    MCLECallPath(@"MainPanel.CheckboxOnline", @"Init", @[MCLEStr(IDS_ONLINE_GAME), @5, @NO]);

    if (idx >= 0 && idx < mcle_saves_count()) {
        MCLECallPath(@"MainPanel.GameName", @"SetLabel",
                     @[[NSString stringWithUTF8String:mcle_saves_name(idx)]]);
        const char* seed = mcle_saves_seed(idx);
        NSString* seedLabel = (seed && seed[0])
            ? [NSString stringWithFormat:@"%@: %s", MCLEStr(IDS_SEED), seed] : @"";
        MCLECallPath(@"MainPanel.Seed", @"SetLabel", @[seedLabel]);

        NSString* thumb = [NSString stringWithUTF8String:mcle_saves_thumbnail(idx)];
        NSData* thumbData = thumb.length ? [NSData dataWithContentsOfFile:thumb] : nil;
        if (thumbData.length) {
            const char* tex = mcle_saves_folder(idx);
            ruffle_ios_register_xui_bitmap(g_ruffle_player,
                (const uint8_t*)tex, strlen(tex),
                (const uint8_t*)thumbData.bytes, thumbData.length, 1.0f, 1.0f);
            MCLECallPath(@"MainPanel.LevelIcon", @"SetTextureName",
                         @[[NSString stringWithUTF8String:tex]]);
        }

        MCLECallPath(@"MainPanel.CreatedMode", @"SetLabel",
                     @[MCLEStr(g_load_has_been_in_creative ? IDS_CREATED_IN_CREATIVE : IDS_CREATED_IN_SURVIVAL)]);
        MCLECallPath(@"MainPanel.GameModeToggle", @"SetLabel",
                     @[MCLEStr(g_load_game_mode == 1 ? IDS_GAMEMODE_CREATIVE : IDS_GAMEMODE_SURVIVAL)]);
    }

    if (!g_texture_pack_waiting_for_dlc) MCLEFillTexturePackList();

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

- (void)loadMenuSlider:(int)sliderId value:(int)value {
    if (sliderId != 1) return;   // eControl_Difficulty
    mcle_settings_set(MCLE_SETTING_Difficulty, (unsigned char)MAX(0, MIN(value, 3)));
    MCLECallPath(@"MainPanel.Difficulty", @"SetLabel", @[MCLEDifficultyLabel(value)]);
}

- (void)loadMenuPress:(int)controlId child:(int)childId {
    if (g_texture_pack_waiting_for_dlc) return;
    mcle_audio_play_ui_sfx("press", 1.0f, 1.0f);
    switch (controlId) {
        case 0: {   // eControl_GameMode
            g_load_game_mode = (g_load_game_mode == 0) ? 1 : 0;
            MCLECallPath(@"MainPanel.GameModeToggle", @"SetLabel",
                         @[MCLEStr(g_load_game_mode == 1 ? IDS_GAMEMODE_CREATIVE : IDS_GAMEMODE_SURVIVAL)]);
            break;
        }
        case 2:     // eControl_MoreOptions
            [self navigateForwardTo:@"LaunchMoreOptionsMenu1080.swf"];
            break;
        case 3:     // eControl_LoadWorld
            [self loadMenuStartSharedLaunchFlow];
            break;
        case 4:     // eControl_TexturePackList
            MCLEUpdateCurrentTexturePack(childId);
            break;
        default:
            break;
    }
}

// IUIScene_StartGame::handleSelectionChanged, and what tick does with it
- (void)startGameSelectionChanged:(int)selectedId {
    // m_iSetTexturePackDescription
    MCLEUpdateTexturePackDescription(selectedId);
    // m_bShowTexturePackDescription
    if (!g_texture_pack_desc_displayed) {
        MCLECallPath(@"", @"SlideLeft", @[]);
        g_texture_pack_desc_displayed = true;
    }
}

// StartSharedLaunchFlow's check that the selected texture pack is there.
// TexturePackDialogReturned only takes the input back off hold, there's
// no store to get it from. YES when it isn't there and the dialog is up.
- (BOOL)startGameTexturePackMissing {
    if (g_start_options.dwTexturePack == 0 || mcle_texture_pack_installed(g_start_options.dwTexturePack)) return NO;
    [self presentDialogWithTitle:MCLEStr(IDS_DLC_TEXTUREPACK_NOT_PRESENT_TITLE)
                         content:MCLEStr(IDS_DLC_TEXTUREPACK_NOT_PRESENT)
                         buttons:@[MCLEStr(IDS_TEXTUREPACK_FULLVERSION), MCLEStr(IDS_CONFIRM_CANCEL)]
                      focusIndex:0
                        callback:^(MCLEDialogResult r) {
        g_create_ignore_input = false;
    }];
    return YES;
}

// UIScene_LoadMenu::StartSharedLaunchFlow: the texture pack has to be there,
// confirm a Nether reset, then LaunchGame
- (void)loadMenuStartSharedLaunchFlow {
    // Check if we need to upsell the texture pack
    if ([self startGameTexturePackMissing]) return;
    if (g_start_options.bResetNether) {
        __weak MinecraftViewController* weakSelf = self;
        [self presentDialogWithTitle:MCLEStr(IDS_RESETNETHER_TITLE)
                             content:MCLEStr(IDS_RESETNETHER_TEXT)
                             buttons:@[MCLEStr(IDS_DONT_RESET_NETHER), MCLEStr(IDS_RESET_NETHER)]
                          focusIndex:0
                            callback:^(MCLEDialogResult r) {
            // CheckResetNetherReturned: results switched for this dialog
            if (r == MCLEDialogResultDecline) {
                [weakSelf loadMenuLaunchGame];
            } else if (r == MCLEDialogResultAccept) {
                g_start_options.bResetNether = false;
                [weakSelf loadMenuLaunchGame];
            }
        }];
        return;
    }
    [self loadMenuLaunchGame];
}

// UIScene_LoadMenu::LaunchGame: creative and host privileges confirm prompts, then load.
- (void)loadMenuLaunchGame {
    BOOL creative = (g_load_game_mode == 1);
    int textId = 0;
    if (creative || g_load_has_been_in_creative) {
        if (!creative) textId = IDS_CONFIRM_START_SAVEDINCREATIVE;
        else if (!g_load_has_been_in_creative) textId = IDS_CONFIRM_START_CREATIVE;
    } else if (g_start_options.bHostPrivileges) {
        textId = IDS_CONFIRM_START_HOST_PRIVILEGES;
    }
    if (textId) {
        __weak MinecraftViewController* weakSelf = self;
        [self presentDialogWithTitle:MCLEStr(IDS_TITLE_START_GAME)
                             content:MCLEStr(textId)
                             buttons:@[MCLEStr(IDS_CONFIRM_OK), MCLEStr(IDS_CONFIRM_CANCEL)]
                          focusIndex:0
                            callback:^(MCLEDialogResult r) {
            if (r == MCLEDialogResultAccept) [weakSelf startLoadingSelectedSave];
        }];
        return;
    }
    [self startLoadingSelectedSave];
}

// StartGameFromSave -> NavigateToScene(eUIScene_FullscreenProgress)
- (void)startLoadingSelectedSave {
    int idx = g_load_save_index;
    if (idx < 0 || idx >= mcle_saves_count() || g_loading_world) return;
    NSString* folder = [NSString stringWithUTF8String:mcle_saves_folder(idx)];
    mcle_storage_set_save_folder(folder.UTF8String);
    mcle_game_set_load_options(g_start_options, g_load_is_save_owner,
                               MAX(0, MIN((int)mcle_settings_get(MCLE_SETTING_Difficulty), 3)),
                               g_load_game_mode);
    [self navigateForwardTo:@"FullscreenProgress1080.swf"];
    if (mcle_game_start(folder.UTF8String)) {
        g_loading_world = true;
    } else {
        NSLog(@"[loadmenu] mcle_game_start refused for %@", folder);
    }
}

// UIScene_CreateWorldMenu::KeyboardCompleteWorldNameCallback. The keyboard
// has gone back but the menu's movie only reloads after, where the name's
// label and the Create button come from m_worldName.
static int MCLECreateWorldKeyboardComplete(void* lpParam, const bool bRes) {
    g_create_ignore_input = false;
    // 4J HEG - No reason to set value if keyboard was cancelled
    if (bRes) {
        // Win64_GetKeyboardText
        const std::wstring& text = mcle_keyboard_text();
        if (!text.empty()) g_create_world_name = MCLEStringFromWide(text);
    }
    return 0;
}

static NSString* MCLECreateWorldGameModeLabel(void) {
    switch (g_create_game_mode_id) {
        case 1:  return MCLEStr(IDS_GAMEMODE_CREATIVE);
        case 2:  return MCLEStr(IDS_GAMEMODE_ADVENTURE);
        default: return MCLEStr(IDS_GAMEMODE_SURVIVAL);
    }
}

// UIScene_CreateWorldMenu constructor. Coming back from More Options or the
// keyboard this is handleGainFocus, the scene's state is still in the
// statics. Control ids follow its EControls: EditWorldName 0,
// TexturePackList 1, GameModeToggle 2, Difficulty 3, MoreOptions 4,
// NewWorld 5, OnlineGame 6.
- (void)initCreateWorldMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    [self attachMenuScenery];
    // No keyboard or dialog outlives the movie
    g_create_ignore_input = false;

    MCLECallPath(@"MainPanel.WorldName", @"Init", @[MCLEStr(IDS_WORLD_NAME)]);
    MCLECallPath(@"MainPanel.EditWorldName", @"Init", @[g_create_world_name, @0]);
    MCLECallPath(@"MainPanel.GameModeToggle", @"Init", @[MCLECreateWorldGameModeLabel(), @2]);
    MCLECallPath(@"MainPanel.MoreOptions", @"Init", @[MCLEStr(IDS_MORE_OPTIONS), @4]);
    MCLECallPath(@"MainPanel.NewWorld", @"Init", @[MCLEStr(IDS_CREATE_NEW_WORLD), @5]);
    MCLECallPath(@"MainPanel.NewWorld", @"EnableButton", @[g_create_world_name.length ? @YES : @NO]);
    MCLECallPath(@"MainPanel.TexturePackSelector", @"Init", @[MCLEStr(IDS_DLC_MENU_TEXTUREPACKS), @1]);
    MCLECallPath(@"TexturePackPanel.TexturePackName", @"Init", @[@""]);
    MCLECallPath(@"TexturePackPanel.TexturePackDescription", @"Init", @[@""]);

    int diff = MAX(0, MIN((int)mcle_settings_get(MCLE_SETTING_Difficulty), 3));
    MCLECallPath(@"MainPanel.Difficulty", @"Init", @[MCLEDifficultyLabel(diff), @3, @0, @3, @(diff)]);

    // Not signed in to an online service, so the box is off and disabled
    MCLECallPath(@"MainPanel.CheckboxOnline", @"EnableCheckBox", @[@NO]);
    MCLECallPath(@"MainPanel.CheckboxOnline", @"Init",
                 @[MCLEStr(IDS_ONLINE_GAME), @6, g_start_options.bOnlineGame ? @YES : @NO]);

    if (!g_texture_pack_waiting_for_dlc) MCLEFillTexturePackList();

    // UIScene::reloadMovie coming back from the keyboard puts the focus back
    // on eControl_EditWorldName, the name it was for
    ruffle_ios_call_root_set_focus(g_ruffle_player, g_menu_reloading ? 0.0 : -1.0);
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// UIScene_CreateWorldMenu::handleSliderMove
- (void)createWorldSlider:(int)sliderId value:(int)value {
    if (sliderId != 3) return;   // eControl_Difficulty
    mcle_settings_set(MCLE_SETTING_Difficulty, (unsigned char)MAX(0, MIN(value, 3)));
    MCLECallPath(@"MainPanel.Difficulty", @"SetLabel", @[MCLEDifficultyLabel(value)]);
}

// UIScene_CreateWorldMenu::handlePress
- (void)createWorldPress:(int)controlId child:(int)childId {
    if (g_create_ignore_input || g_texture_pack_waiting_for_dlc) return;
    mcle_audio_play_ui_sfx("press", 1.0f, 1.0f);
    switch (controlId) {
        case 0: {   // eControl_EditWorldName
            g_create_ignore_input = true;
            // Windows64 without the keyboard and mouse: UIScene_Keyboard
            MCLEKeyboardInitData kbData;
            kbData.title = MCLEWideString(MCLEStr(IDS_CREATE_NEW_WORLD));
            if (!MCLELabelAt("MainPanel.EditWorldName", &kbData.defaultText)) {
                kbData.defaultText = MCLEWideString(g_create_world_name);
            }
            kbData.maxChars = 25;
            kbData.callback = &MCLECreateWorldKeyboardComplete;
            [self navigateToKeyboard:kbData];
            break;
        }
        case 2:     // eControl_GameModeToggle
            switch (g_create_game_mode_id) {
                case 0: // Creative
                    g_create_game_mode_id = 1;
                    g_create_game_mode_creative = true;
                    break;
                case 1: // Adventure
                    g_create_game_mode_id = 2;
                    g_create_game_mode_creative = false;
                    break;
                case 2: // Survival
                    g_create_game_mode_id = 0;
                    g_create_game_mode_creative = false;
                    break;
            }
            MCLECallPath(@"MainPanel.GameModeToggle", @"SetLabel", @[MCLECreateWorldGameModeLabel()]);
            break;
        case 4:     // eControl_MoreOptions
            [self navigateForwardTo:@"LaunchMoreOptionsMenu1080.swf"];
            break;
        case 1:     // eControl_TexturePackList
            MCLEUpdateCurrentTexturePack(childId);
            break;
        case 5:     // eControl_NewWorld
            [self createWorldStartSharedLaunchFlow];
            break;
        default:
            break;
    }
}

// UIScene_CreateWorldMenu::StartSharedLaunchFlow and checkStateAndStartGame.
// Offline there are no sign in or online checks, only the start game warnings.
- (void)createWorldStartSharedLaunchFlow {
    // Check if we need to upsell the texture pack
    if ([self startGameTexturePackMissing]) return;
    g_create_ignore_input = true;

    // Only save the online setting if the user changed it - we may change it because we're offline, but don't want that saved
    if (!g_start_options.bOnlineSettingChangedBySystem) {
        mcle_settings_set(MCLE_SETTING_Online, g_start_options.bOnlineGame ? 1 : 0);
    }
    mcle_settings_set(MCLE_SETTING_InviteOnly, g_start_options.bInviteOnly ? 1 : 0);
    mcle_settings_set(MCLE_SETTING_FriendsOfFriends, g_start_options.bAllowFriendsOfFriends ? 1 : 0);

    if (g_create_game_mode_creative || g_start_options.bHostPrivileges) {
        int textId = g_create_game_mode_creative ? IDS_CONFIRM_START_CREATIVE : IDS_CONFIRM_START_HOST_PRIVILEGES;
        __weak MinecraftViewController* weakSelf = self;
        [self presentDialogWithTitle:MCLEStr(IDS_TITLE_START_GAME)
                             content:MCLEStr(textId)
                             buttons:@[MCLEStr(IDS_CONFIRM_OK), MCLEStr(IDS_CONFIRM_CANCEL)]
                          focusIndex:0
                            callback:^(MCLEDialogResult r) {
            // ConfirmCreateReturned
            if (r == MCLEDialogResultAccept) [weakSelf createWorldCreateGame];
            else g_create_ignore_input = false;
        }];
        return;
    }
    [self createWorldCreateGame];
}

// UIScene_CreateWorldMenu::CreateGame, the game side is mcle_game_create_world
- (void)createWorldCreateGame {
    if (g_loading_world) return;
    int difficulty = MAX(0, MIN((int)mcle_settings_get(MCLE_SETTING_Difficulty), 3));
    std::wstring worldName = MCLEWideString(g_create_world_name);
    [self navigateForwardTo:@"FullscreenProgress1080.swf"];
    if (mcle_game_create_world(g_start_options, worldName, g_create_game_mode_id, difficulty)) {
        g_loading_world = true;
    } else {
        NSLog(@"[createworld] mcle_game_create_world refused");
    }
}

// UIScene_LaunchMoreOptionsMenu's checkboxes, by EControls id
static const char* const kMoreOptionsCheckboxes[21] = {
    "GameOptions.CheckboxOnline",               // eLaunchCheckbox_Online
    "GameOptions.CheckboxInviteOnly",           // eLaunchCheckbox_InviteOnly
    "GameOptions.CheckboxAllowFoF",             // eLaunchCheckbox_AllowFoF
    "GameOptions.CheckboxPVP",                  // eLaunchCheckbox_PVP
    "WorldOptions.CheckboxTrustSystem",         // eLaunchCheckbox_TrustSystem
    "WorldOptions.CheckboxFireSpreads",         // eLaunchCheckbox_FireSpreads
    "WorldOptions.CheckboxTNT",                 // eLaunchCheckbox_TNT
    "GameOptions.CheckboxHostPrivileges",       // eLaunchCheckbox_HostPrivileges
    "WorldOptions.CheckboxResetNether",         // eLaunchCheckbox_ResetNether
    "WorldOptions.CheckboxStructures",          // eLaunchCheckbox_Structures
    "WorldOptions.CheckboxFlatWorld",           // eLaunchCheckbox_FlatWorld
    "WorldOptions.CheckboxBonusChest",          // eLaunchCheckbox_BonusChest
    "WorldOptions.CheckboxDisableSaving",       // eLaunchCheckbox_DisableSaving
    "WorldOptions.CheckboxResizeType",          // eLaunchCheckbox_WorldResizeType
    "GameOptions.CheckboxKeepInventory",        // eLaunchCheckbox_KeepInventory
    "GameOptions.CheckboxMobSpawning",          // eLaunchCheckbox_MobSpawning
    "GameOptions.CheckboxMobLoot",              // eLaunchCheckbox_MobLoot
    "GameOptions.CheckboxMobGriefing",          // eLaunchCheckbox_MobGriefing
    "GameOptions.CheckboxTileDrops",            // eLaunchCheckbox_TileDrops
    "GameOptions.CheckboxNaturalRegeneration",  // eLaunchCheckbox_NaturalRegeneration
    "GameOptions.CheckboxDayLightCycle",        // eLaunchCheckbox_DayLightCycle
};
static const int kMoreOptionsEditSeed = 22;
static const int kMoreOptionsWorldSize = 23;
static const int kMoreOptionsWorldResize = 24;

static const int kWorldSizeTitles[4] = {
    IDS_WORLD_SIZE_TITLE_CLASSIC,
    IDS_WORLD_SIZE_TITLE_SMALL,
    IDS_WORLD_SIZE_TITLE_MEDIUM,
    IDS_WORLD_SIZE_TITLE_LARGE,
};

static void MCLEMoreOptionsCheckbox(int box, NSString* method, BOOL value) {
    MCLECallPath(@(kMoreOptionsCheckboxes[box]), method, @[value ? @YES : @NO]);
}

static void MCLEMoreOptionsInitCheckbox(int box, int ids, bool checked) {
    MCLECallPath(@(kMoreOptionsCheckboxes[box]), @"Init", @[MCLEStr(ids), @(box), checked ? @YES : @NO]);
}

// UIScene_LaunchMoreOptionsMenu::UpdateCheats: the game rules that need host privileges
static void MCLEMoreOptionsUpdateCheats(void) {
    MCLELaunchMoreOptions& p = g_start_options;
    bool cheatsOn = p.bHostPrivileges;

    MCLEMoreOptionsCheckbox(14, @"EnableCheckBox", cheatsOn);   // KeepInventory
    MCLEMoreOptionsCheckbox(15, @"EnableCheckBox", cheatsOn);   // MobSpawning
    MCLEMoreOptionsCheckbox(17, @"EnableCheckBox", cheatsOn);   // MobGriefing
    MCLEMoreOptionsCheckbox(20, @"EnableCheckBox", cheatsOn);   // DayLightCycle

    if (!cheatsOn) {
        // Set defaults
        p.bMobGriefing = true;
        p.bKeepInventory = false;
        p.bDoMobSpawning = true;
        p.bDoDaylightCycle = true;

        MCLEMoreOptionsCheckbox(14, @"SetCheckBox", p.bKeepInventory);
        MCLEMoreOptionsCheckbox(15, @"SetCheckBox", p.bDoMobSpawning);
        MCLEMoreOptionsCheckbox(17, @"SetCheckBox", p.bMobGriefing);
        MCLEMoreOptionsCheckbox(20, @"SetCheckBox", p.bDoDaylightCycle);
    }
}

// UIScene_LaunchMoreOptionsMenu::UpdateOnline
static void MCLEMoreOptionsUpdateOnline(void) {
    bool bOnline = g_start_options.bOnlineGame;
    MCLEMoreOptionsCheckbox(1, @"EnableCheckBox", bOnline);   // InviteOnly
    MCLEMoreOptionsCheckbox(2, @"EnableCheckBox", bOnline);   // AllowFoF
}

// The active tab's description text, in eHTMLColor_White
static void MCLEMoreOptionsSetDescription(int stringId) {
    NSString* text = [NSString stringWithFormat:@"<font color=\"#%08x\">%@", 0xffffffffu, MCLEStr(stringId)];
    MCLECallPath(g_more_options_tab == 0 ? @"WorldOptions.Description_WorldOptions"
                                         : @"GameOptions.Description_GameOptions",
                 @"SetLabel", @[text]);
}

// UIScene_LaunchMoreOptionsMenu::updateTooltips: LB shows without text, RB names the other tab
static void MCLEMoreOptionsTooltips(void) {
    int changeTabTooltip = g_more_options_tab == 1 ? IDS_WORLD_OPTIONS : IDS_GAME_OPTIONS;
    int ids[11] = { IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK, -1, -1, -1, -1, -2, changeTabTooltip, -1, -1, -1 };
    MCLESetTooltips(ids);
}

// UIScene_LaunchMoreOptionsMenu::handleInput, LB or RB
static void MCLEMoreOptionsChangeTab(void) {
    // Toggle tab index
    g_more_options_tab = g_more_options_tab == 0 ? 1 : 0;
    MCLEMoreOptionsTooltips();
    MCLECallPath(@"", @"ChangeTab", @[]);
}

// UIScene_LaunchMoreOptionsMenu constructor, on the params of the
// CreateWorldMenu or LoadMenu that opened it. Coming back from the keyboard
// it's reloadMovie, on the same params.
- (void)initMoreOptionsMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    [self attachMenuScenery];
    MCLELaunchMoreOptions& p = g_start_options;
    g_more_options_ignore_input = false;

    MCLECallPath(@"LabelWorld", @"Init", @[MCLEStr(IDS_WORLD_OPTIONS)]);

    // don't show the increase world size stuff if we're already large, or the size is unknown
    bool showResize = p.currentWorldSize == 1 || p.currentWorldSize == 2 || p.currentWorldSize == 3;
    MCLECallPath(@"", @"SetMenuType", @[@(p.bGenerateOptions ? 0 : 1), showResize ? @YES : @NO]);

    bool bOnlineGame = p.bOnlineGame;
    bool bInviteOnly = p.bInviteOnly;
    bool bAllowFriendsOfFriends = p.bAllowFriendsOfFriends;

    // Not signed in to an online service, so multiplayer isn't allowed
    MCLEMoreOptionsCheckbox(0, @"EnableCheckBox", NO);
    if (p.bOnlineSettingChangedBySystem) {
        MCLEMoreOptionsCheckbox(0, @"EnableCheckBox", NO);
        MCLEMoreOptionsCheckbox(1, @"EnableCheckBox", NO);
        MCLEMoreOptionsCheckbox(2, @"EnableCheckBox", NO);
        bOnlineGame = bInviteOnly = bAllowFriendsOfFriends = false;
    } else if (!p.bOnlineGame) {
        MCLEMoreOptionsCheckbox(1, @"EnableCheckBox", NO);
        MCLEMoreOptionsCheckbox(2, @"EnableCheckBox", NO);
    }

    MCLEMoreOptionsUpdateCheats();

    MCLEMoreOptionsInitCheckbox(0, IDS_ONLINE_GAME, bOnlineGame);
    MCLEMoreOptionsInitCheckbox(1, IDS_INVITE_ONLY, bInviteOnly);
    MCLEMoreOptionsInitCheckbox(2, IDS_ALLOWFRIENDSOFFRIENDS, bAllowFriendsOfFriends);
    MCLEMoreOptionsInitCheckbox(3, IDS_PLAYER_VS_PLAYER, p.bPVP);
    MCLEMoreOptionsInitCheckbox(4, IDS_TRUST_PLAYERS, p.bTrust);
    MCLEMoreOptionsInitCheckbox(5, IDS_FIRE_SPREADS, p.bFireSpreads);
    MCLEMoreOptionsInitCheckbox(6, IDS_TNT_EXPLODES, p.bTNT);
    MCLEMoreOptionsInitCheckbox(7, IDS_HOST_PRIVILEGES, p.bHostPrivileges);
    MCLEMoreOptionsInitCheckbox(8, IDS_RESET_NETHER, p.bResetNether);
    MCLEMoreOptionsInitCheckbox(9, IDS_GENERATE_STRUCTURES, p.bStructures);
    MCLEMoreOptionsInitCheckbox(10, IDS_SUPERFLAT_WORLD, p.bFlatWorld);
    MCLEMoreOptionsInitCheckbox(11, IDS_BONUS_CHEST, p.bBonusChest);

    MCLEMoreOptionsInitCheckbox(14, IDS_KEEP_INVENTORY, p.bKeepInventory);
    MCLEMoreOptionsInitCheckbox(15, IDS_MOB_SPAWNING, p.bDoMobSpawning);
    MCLEMoreOptionsInitCheckbox(16, IDS_MOB_LOOT, p.bDoMobLoot);
    MCLEMoreOptionsInitCheckbox(17, IDS_MOB_GRIEFING, p.bMobGriefing);
    MCLEMoreOptionsInitCheckbox(18, IDS_TILE_DROPS, p.bDoTileDrops);
    MCLEMoreOptionsInitCheckbox(19, IDS_NATURAL_REGEN, p.bNaturalRegeneration);
    MCLEMoreOptionsInitCheckbox(20, IDS_DAYLIGHT_CYCLE, p.bDoDaylightCycle);

    MCLECallPath(@"LabelGame", @"Init", @[MCLEStr(IDS_GAME_OPTIONS)]);
    MCLECallPath(@"WorldOptions.Seed", @"Init", @[MCLEStr(IDS_CREATE_NEW_WORLD_SEED)]);
    MCLECallPath(@"WorldOptions.RandomSeed", @"Init", @[MCLEStr(IDS_CREATE_NEW_WORLD_RANDOM_SEED)]);
    MCLECallPath(@"WorldOptions.EditSeed", @"Init", @[MCLEStringFromUTF16(p.seed), @(kMoreOptionsEditSeed)]);

    int worldSize = MAX(0, MIN(p.worldSize, 3));
    MCLECallPath(@"WorldOptions.WorldSize", @"Init", @[MCLEStr(IDS_WORLD_SIZE)]);
    MCLECallPath(@"WorldOptions.WorldSizeSlider", @"Init",
                 @[MCLEStr(kWorldSizeTitles[worldSize]), @(kMoreOptionsWorldSize), @0, @3, @(worldSize)]);

    MCLEMoreOptionsInitCheckbox(12, IDS_DISABLE_SAVING, p.bDisableSaving);

    if (p.currentWorldSize != 0) {
        MCLECallPath(@"WorldOptions.ResizeLabel", @"Init", @[MCLEStr(IDS_INCREASE_WORLD_SIZE)]);
        int min = p.currentWorldSize - 1;
        int curr = MAX(0, MIN(p.newWorldSize - 1, 3));
        MCLECallPath(@"WorldOptions.ChangeWorldSizeSlider", @"Init",
                     @[MCLEStr(kWorldSizeTitles[curr]), @(kMoreOptionsWorldResize), @(min), @3, @(curr)]);
        MCLEMoreOptionsInitCheckbox(13, IDS_INCREASE_WORLD_SIZE_OVERWRITE_EDGES, p.newWorldSizeOverwriteEdges);
    }

    g_more_options_tab = p.bGenerateOptions ? 0 : 1;
    // set the default text
    MCLEMoreOptionsSetDescription(p.bGenerateOptions ? IDS_GAMEOPTION_SEED : IDS_GAMEOPTION_ONLINE);

    // UIScene::reloadMovie coming back from the keyboard puts the focus back
    // on the seed it was for
    if (g_menu_reloading) ruffle_ios_call_root_set_focus(g_ruffle_player, kMoreOptionsEditSeed);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// UIScene_LaunchMoreOptionsMenu::handleCheckboxToggled
- (void)moreOptionsCheckbox:(int)controlId checked:(BOOL)selected {
    // A box that hasn't had its Init yet
    if (controlId < 0) return;
    mcle_audio_play_ui_sfx("press", 1.0f, 1.0f);
    MCLELaunchMoreOptions& p = g_start_options;
    switch (controlId) {
        case 0:  p.bOnlineGame = selected; MCLEMoreOptionsUpdateOnline(); break;
        case 1:  p.bInviteOnly = selected; break;
        case 2:  p.bAllowFriendsOfFriends = selected; break;
        case 3:  p.bPVP = selected; break;
        case 4:  p.bTrust = selected; break;
        case 5:  p.bFireSpreads = selected; break;
        case 6:  p.bTNT = selected; break;
        case 7:  p.bHostPrivileges = selected; MCLEMoreOptionsUpdateCheats(); break;
        case 8:  p.bResetNether = selected; break;
        case 9:  p.bStructures = selected; break;
        case 10: p.bFlatWorld = selected; break;
        case 11: p.bBonusChest = selected; break;
        case 12: p.bDisableSaving = selected; break;
        case 13: p.newWorldSizeOverwriteEdges = selected; break;
        case 14: p.bKeepInventory = selected; break;
        case 15: p.bDoMobSpawning = selected; break;
        // upstream's MobLoot case has no break and carries on into MobGriefing
        case 16: p.bDoMobLoot = selected; p.bMobGriefing = selected; break;
        case 17: p.bMobGriefing = selected; break;
        case 18: p.bDoTileDrops = selected; break;
        case 19: p.bNaturalRegeneration = selected; break;
        case 20: p.bDoDaylightCycle = selected; break;
        default: break;
    }
}

// UIScene_LaunchMoreOptionsMenu::handleFocusChange: the focused option's description
- (void)moreOptionsFocus:(int)controlId {
    static const int kDescriptions[25] = {
        IDS_GAMEOPTION_ONLINE, IDS_GAMEOPTION_INVITEONLY, IDS_GAMEOPTION_ALLOWFOF, IDS_GAMEOPTION_PVP,
        IDS_GAMEOPTION_TRUST, IDS_GAMEOPTION_FIRE_SPREADS, IDS_GAMEOPTION_TNT_EXPLODES,
        IDS_GAMEOPTION_HOST_PRIVILEGES, IDS_GAMEOPTION_RESET_NETHER, IDS_GAMEOPTION_STRUCTURES,
        IDS_GAMEOPTION_SUPERFLAT, IDS_GAMEOPTION_BONUS_CHEST, IDS_GAMEOPTION_DISABLE_SAVING,
        IDS_GAMEOPTION_INCREASE_WORLD_SIZE_OVERWRITE_EDGES, IDS_GAMEOPTION_KEEP_INVENTORY,
        IDS_GAMEOPTION_MOB_SPAWNING, IDS_GAMEOPTION_MOB_LOOT, IDS_GAMEOPTION_MOB_GRIEFING,
        IDS_GAMEOPTION_TILE_DROPS, IDS_GAMEOPTION_NATURAL_REGEN, IDS_GAMEOPTION_DAYLIGHT_CYCLE,
        0, IDS_GAMEOPTION_SEED, IDS_GAMEOPTION_WORLD_SIZE, IDS_GAMEOPTION_INCREASE_WORLD_SIZE,
    };
    int stringId = (controlId >= 0 && controlId < 25) ? kDescriptions[controlId] : 0;
    MCLEMoreOptionsSetDescription(stringId);
}

// UIScene_LaunchMoreOptionsMenu::handleSliderMove
- (void)moreOptionsSlider:(int)sliderId value:(int)value {
    if (value < 0 || value > 3) return;
    if (sliderId == kMoreOptionsWorldSize) {
        g_start_options.worldSize = value;
        MCLECallPath(@"WorldOptions.WorldSizeSlider", @"SetLabel", @[MCLEStr(kWorldSizeTitles[value])]);
    } else if (sliderId == kMoreOptionsWorldResize) {
        if (value + 1 >= g_start_options.currentWorldSize) {
            g_start_options.newWorldSize = value + 1;
            MCLECallPath(@"WorldOptions.ChangeWorldSizeSlider", @"SetLabel", @[MCLEStr(kWorldSizeTitles[value])]);
        }
    }
}

// UIScene_LaunchMoreOptionsMenu::KeyboardCompleteSeedCallback. The menu's
// movie reloads once the keyboard has gone, and the seed's label comes from
// m_params->seed there.
static int MCLEMoreOptionsKeyboardComplete(void* lpParam, const bool bRes) {
    g_more_options_ignore_input = false;
    if (bRes) {
        // Win64_GetKeyboardText
        g_start_options.seed = MCLEWideUTF16(MCLEStringFromWide(mcle_keyboard_text()));
    }
    return 0;
}

// UIScene_LaunchMoreOptionsMenu::handlePress, the seed's keyboard
- (void)moreOptionsPress:(int)controlId {
    if (g_more_options_ignore_input || controlId != kMoreOptionsEditSeed) return;
    // Windows64 without the keyboard and mouse: UIScene_Keyboard
    g_more_options_ignore_input = true;
    MCLEKeyboardInitData kbData;
    kbData.title = MCLEWideString(MCLEStr(IDS_CREATE_NEW_WORLD_SEED));
    if (!MCLELabelAt("WorldOptions.EditSeed", &kbData.defaultText)) {
        kbData.defaultText = MCLEWideString(MCLEStringFromUTF16(g_start_options.seed));
    }
    kbData.maxChars = 60;
    kbData.callback = &MCLEMoreOptionsKeyboardComplete;
    [self navigateToKeyboard:kbData];
}

static int g_progress_stage = -2;
static int g_progress_pct = -1;
// UIScene_FullscreenProgress's m_lastTitle and m_lastStatus
static int g_progress_title = -1;
static int g_progress_status = -1;
static CFTimeInterval g_progress_tip_time = 0;

static NSString* MCLETipHTML(void) {
    // FullscreenProgress wraps each tip like this (eHTMLColor_White)
    return [NSString stringWithFormat:@"<font color=\"#ffffffff\"><p align=center>%@</p>",
            MCLEStr(MCLENextTip())];
}

// UIScene_FullscreenProgress::handleTimerComplete, the next tip every
// TIMER_FULLSCREEN_TIPS_TIME
static const CFTimeInterval kProgressTipTime = 7.0;
static void MCLEProgressTipTick(void) {
    CFTimeInterval now = CACurrentMediaTime();
    if (now - g_progress_tip_time >= kProgressTipTime) {
        g_progress_tip_time = now;
        MCLECallPath(@"Tip", @"SetLabel", @[MCLETipHTML()]);
    }
}

// Longer Loading Screens: the screen stays up until two tips have shown.
// What's ready by then waits under it with the game paused, so nothing
// ticks that the player can't see.
static CFTimeInterval g_loading_hold_start = 0;
static bool g_loading_hold_paused = false;

static bool MCLELoadingHeld(void) {
    return mcle_settings_get_longer_loading() &&
           CACurrentMediaTime() - g_loading_hold_start < 2 * kProgressTipTime;
}

static void MCLELoadingHoldPause(bool paused) {
    if (!paused && !g_loading_hold_paused) return;
    g_loading_hold_paused = paused;
    mcle_game_set_paused(paused ? 1 : 0);
}

// UIScene_FullscreenProgress constructor
- (void)initFullscreenProgress {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    [self attachMenuScenery];
    MCLECallPath(@"Confirm", @"Init", @[MCLEStr(IDS_CONFIRM_OK), @0]);
    MCLESetPathVisible(@"Confirm", NO);
    MCLECallPath(@"ProgressBar", @"Init", @[@""]);
    MCLECallPath(@"Tip", @"Init", @[MCLETipHTML()]);
    MCLECallPath(@"Title", @"Init", @[@""]);
    MCLESetPathVisible(@"Tip", YES);
    g_progress_stage = -2;
    g_progress_pct = -1;
    g_progress_tip_time = CACurrentMediaTime();
    g_loading_hold_start = g_progress_tip_time;
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// FullscreenProgress completes with e_ProgressCompletion_CloseAllPlayersUIScenes,
// and the panorama, logo and menu background components hide once the game
// has started.
// UIScene_PauseMenu. Opening it pauses the single player game.
- (void)openPauseMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    g_world_paused = true;
    mcle_game_set_paused(1);
    // NavigateToScene's SetMenuDisplayed, the pad drives the menu now
    mcle_game_set_menu_displayed(true);
    // UIScene_PauseMenu: the tutorial popup stays hidden while it's up
    mcle_tutorial_show_popup(false);
    self.menuStack = nil;
    [self transitionToMenuNamed:@"PauseMenu1080.swf"];
}

- (void)initPauseMenu {
    extern PlayerHandle* g_ruffle_player;
    NSArray<NSDictionary*>* cfg = @[
        @{ @"name": @"Button1", @"label": MCLEStr(IDS_RESUME_GAME),      @"id": @(0) },
        @{ @"name": @"Button2", @"label": MCLEStr(IDS_HELP_AND_OPTIONS), @"id": @(1) },
        @{ @"name": @"Button3", @"label": MCLEStr(IDS_LEADERBOARDS),     @"id": @(2) },
        @{ @"name": @"Button4", @"label": MCLEStr(IDS_ACHIEVEMENTS),     @"id": @(3) },
        @{ @"name": @"Button5", @"label": MCLEStr(IDS_SAVE_GAME),        @"id": @(4) },
        @{ @"name": @"Button6", @"label": MCLEStr(IDS_EXIT_GAME),        @"id": @(5) },
    ];
    [self applyMenuButtonConfig:cfg];
    // updateComponents: no panorama, menu background and logo on
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, -2, 0);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 1);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kLogoDepth, 1);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTooltipsDepth, 1);
}

// The calls a container scene queued, made on the movie
static void MCLEContainerApplyOps(void) {
    extern PlayerHandle* g_ruffle_player;
    for (const MCLEUIOp& op : mcle_container_take_ops()) {
        if (op.kind == MCLEUIOp::Sound) {
            float pitch = op.args.empty() ? 1.0f : static_cast<float>(op.args[0].number);
            mcle_audio_play_ui_sfx(op.method.c_str(), 1.0f, pitch);
            continue;
        }
        if (op.kind == MCLEUIOp::Key) {
            if (g_ruffle_player && op.args.size() == 2) {
                ruffle_ios_player_key(g_ruffle_player, static_cast<int>(op.args[0].number), op.args[1].number != 0);
            }
            continue;
        }
        if (op.kind == MCLEUIOp::AnimateKey) {
            if (op.args.size() == 1) MCLEAnimateKeyPress(static_cast<int>(op.args[0].number));
            continue;
        }
        if (op.kind == MCLEUIOp::Visible) {
            if (op.args.size() == 1) {
                MCLESetPathVisible([NSString stringWithUTF8String:op.path.c_str()], op.args[0].number != 0);
            }
            continue;
        }
        NSMutableArray* args = [NSMutableArray arrayWithCapacity:op.args.size()];
        for (const MCLEUIArg& a : op.args) {
            if (a.kind == MCLEUIArg::String) {
                NSString* s = [NSString stringWithUTF8String:a.str.c_str()];
                [args addObject:s ? s : @""];
            } else if (a.kind == MCLEUIArg::Bool) {
                [args addObject:a.number != 0 ? @YES : @NO];
            } else {
                [args addObject:@(a.number)];
            }
        }
        MCLECallPath([NSString stringWithUTF8String:op.path.c_str()],
                     [NSString stringWithUTF8String:op.method.c_str()], args);
    }
}

// The calls the tutorial popup queued, made on its own movie
static void MCLETutorialPopupApplyOps(void) {
    extern PlayerHandle* g_ruffle_player;
    std::vector<MCLEUIOp> ops = mcle_tutorial_popup_take_ops();
    if (!g_ruffle_player) return;
    for (const MCLEUIOp& op : ops) {
        if (op.kind == MCLEUIOp::Visible) {
            if (op.args.size() == 1) {
                ruffle_ios_set_sibling_path_visible(g_ruffle_player, kTutorialPopupDepth,
                    (const uint8_t*)op.path.c_str(), op.path.size(), op.args[0].number != 0 ? 1 : 0);
            }
            continue;
        }
        if (op.kind != MCLEUIOp::Call) continue;
        size_t n = op.args.size();
        std::vector<int> kinds(n);
        std::vector<double> nums(n);
        std::vector<const uint8_t*> strs(n);
        std::vector<size_t> lens(n);
        for (size_t i = 0; i < n; ++i) {
            const MCLEUIArg& a = op.args[i];
            kinds[i] = a.kind == MCLEUIArg::String ? 2 : (a.kind == MCLEUIArg::Bool ? 1 : 0);
            nums[i] = a.number;
            strs[i] = (const uint8_t*)a.str.c_str();
            lens[i] = a.str.size();
        }
        ruffle_ios_call_sibling_path(g_ruffle_player, kTutorialPopupDepth,
            (const uint8_t*)op.path.c_str(), op.path.size(),
            (const uint8_t*)op.method.c_str(), op.method.size(),
            kinds.data(), nums.data(), strs.data(), lens.data(), n);
    }
}

// UIComponent_TutorialPopup for this frame: its timers, its calls, and the
// layer's visible flag for it. Only shown over the HUD and the menus in game.
static bool g_tutorial_popup_shown = false;
static void MCLETutorialPopupFrame(bool inGame) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    if (inGame) mcle_tutorial_popup_tick();
    MCLETutorialPopupApplyOps();
    bool shown = inGame && mcle_tutorial_popup_is_visible();
    if (shown != g_tutorial_popup_shown) {
        g_tutorial_popup_shown = shown;
        ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTutorialPopupDepth, shown ? 1 : 0);
    }
}

// The popup's iggy_ regions after the movie has drawn, for its customDraw
static void MCLETutorialPopupRegions(void) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    RuffleIosCustomDrawRegion regions[4];
    int n = g_tutorial_popup_shown
        ? ruffle_ios_sibling_custom_draw_regions(g_ruffle_player, kTutorialPopupDepth, regions, 4)
        : 0;
    n = std::max(0, std::min(n, 4));
    mcle_tutorial_popup_set_regions(reinterpret_cast<const MCLECustomDrawRegion*>(regions), n);
}

// UIControl's x, y, width and height for a control, read when the scene maps it
static MCLEControlRect MCLEControlRectAt(const char* path) {
    extern PlayerHandle* g_ruffle_player;
    float r[4] = { 0, 0, 0, 0 };
    if (g_ruffle_player) ruffle_ios_child_path_rect(g_ruffle_player, (const uint8_t*)path, strlen(path), r);
    return MCLEControlRect{ r[0], r[1], r[2], r[3] };
}

// UIControl_Base::getLabel, GetLabel on the control. Iggy would have made
// the calls the scene queued before it already, so they go first.
static bool MCLELabelAt(const char* path, std::wstring* label) {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return false;
    MCLEContainerApplyOps();
    char text[1024];
    int n = ruffle_ios_call_child_path_string(g_ruffle_player, (const uint8_t*)path, strlen(path),
                                              (const uint8_t*)"GetLabel", 8, (uint8_t*)text, sizeof(text));
    if (n < 0) return false;
    NSString* s = [[NSString alloc] initWithBytes:text length:(NSUInteger)n encoding:NSUTF8StringEncoding];
    *label = MCLEWideString(s ?: @"");
    return true;
}

// Minecraft::tick's inventory action: app.LoadInventoryMenu
- (void)openInventoryMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player || !mcle_container_can_open_inventory()) return;
    self.menuStack = nil;
    [self transitionToMenuNamed:@"InventoryMenu1080.swf"];
    [self showContainerOverWorld];
}

// Minecraft::tick's crafting action (app.LoadCrafting2x2Menu), or
// LocalPlayer::startCrafting for the server's crafting table
- (void)openCraftingMenu:(BOOL)table {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) {
        if (table) mcle_container_open_failed();
        return;
    }
    self.menuStack = nil;
    [self transitionToMenuNamed:table ? @"Crafting3x3Menu1080.swf" : @"Crafting2x2Menu1080.swf"];
    if (!mcle_container_is_open() && table) mcle_container_open_failed();
    [self showContainerOverWorld];
}

// The scene the movie just went up for, or back to the HUD if it didn't open
- (void)showContainerOverWorld {
    extern PlayerHandle* g_ruffle_player;
    if (!mcle_container_is_open()) {
        [self enterWorldUI];
        return;
    }
    g_world_container = true;
    // The container scenes don't change the components, so it's the world
    // under it like the HUD had, with the menu background if the scene
    // added it. The tooltips show while a menu is up.
    const int depths[] = { -2, kMenuBackgroundDepth, kLogoDepth, kDimDepth };
    for (int d : depths) ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, d, 0);
    if (mcle_container_menu_background()) {
        ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 1);
    }
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTooltipsDepth, 1);
    MCLEGameTooltipsReset();
}

// ClientConnection::handleTileEditorOpen's LocalPlayer::openTextEdit:
// app.LoadSignEntryMenu
- (void)openSignEntryMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    self.menuStack = nil;
    [self transitionToMenuNamed:@"SignEntryMenu1080.swf"];
    [self showContainerOverWorld];
}

// UIScene_SignEntryMenu's constructor once its movie is up, or its
// reloadMovie coming back from the keyboard
- (void)initSignEntryMenu {
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
    if (g_container_reloading) mcle_container_reload();
    else mcle_sign_entry_open();
    MCLEContainerApplyOps();
}

// Minecraft::tick's game info action: UIScene_InGameInfoMenu over the
// running world, which goes on ticking under it
- (void)openInGameInfoMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    self.menuStack = nil;
    [self transitionToMenuNamed:@"InGameInfoMenu1080.swf"];
    [self showContainerOverWorld];
    g_world_info = mcle_container_is_open();
}

// UIScene_InGameInfoMenu's constructor once its movie is up, or its
// reloadMovie coming back from the host or player options
- (void)initInGameInfoMenu {
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
    if (g_container_reloading) mcle_container_reload();
    else mcle_ingame_info_open();
    MCLEContainerApplyOps();
}

// UIScene_InGameHostOptionsMenu's or UIScene_InGamePlayerOptionsMenu's
// constructor, over the info menu that navigated to it
- (void)initInGameOptionsMenu {
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
    mcle_container_open_navigated();
    MCLEContainerApplyOps();
}

// UIScene_Keyboard's constructor, over the scene that navigated to it
- (void)initKeyboardMenu {
    extern PlayerHandle* g_ruffle_player;
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
    if (mcle_keyboard_front_end_open()) {
        // parentLayer->addComponent(eUIComponent_MenuBackground)
        ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 1);
    } else {
        mcle_container_open_navigated();
    }
    MCLEContainerApplyOps();
}

// ui.NavigateToScene(iPad, eUIScene_Keyboard, &kbData) from a front end
// menu: the keyboard goes on the menu stack over it
- (void)navigateToKeyboard:(const MCLEKeyboardInitData&)kbData {
    NSUInteger depth = self.menuStack.count;
    mcle_keyboard_front_end_navigate_to(0, kbData);
    [self navigateForwardTo:@"Keyboard1080.swf"];
    // Its movie never came up, the menu is still the one up
    if (!mcle_keyboard_front_end_is_open()) {
        while (self.menuStack.count > depth) [self.menuStack removeLastObject];
    }
    mcle_keyboard_front_end_navigate_done();
}

// ~UIScene_Keyboard over a front end menu, and the menu background it added
- (void)closeFrontEndKeyboard {
    extern PlayerHandle* g_ruffle_player;
    mcle_keyboard_front_end_close();
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 0);
}

// A UI frame of the keyboard over a front end menu: the pad
// (UIController::handleInput) and the scene's tick, then the movie. Its
// navigateBack puts the menu's movie back up for its reloadMovie.
- (void)frontEndKeyboardFrame {
    extern PlayerHandle* g_ruffle_player;
    mcle_ios_pad_state pad = {};
    mcle_ios_input_poll(0, &pad);
    mcle_keyboard_front_end_frame(pad.buttons, self.lastPadButtons, CACurrentMediaTime());
    self.lastPadButtons = pad.buttons;
    MCLEContainerApplyOps();
    if (mcle_keyboard_front_end_close_requested()) {
        [self closeFrontEndKeyboard];
        g_menu_reloading = true;
        [self navigateBack];
        g_menu_reloading = false;
        return;
    }
    int tips[11];
    mcle_container_tooltips(tips);
    MCLESetTooltips(tips);
    ruffle_ios_player_tick(g_ruffle_player, 1.0f / 60.0f);
}

// UIController::NavigateToScene from the scene that's up: the new scene's
// movie takes the place of that one's
- (void)containerNavigateTo:(NSString*)movie {
    [self transitionToMenuNamed:movie];
    mcle_container_navigate_done();
    [self showContainerOverWorld];
}

// A navigateBack to the scene under the one that's up: its movie goes back
// up for its reloadMovie
- (void)containerNavigateBack {
    NSString* movie = [NSString stringWithUTF8String:mcle_container_back_movie()];
    mcle_container_navigate_back();
    MCLEContainerApplyOps();
    g_container_reloading = true;
    [self transitionToMenuNamed:movie];
    g_container_reloading = false;
    [self showContainerOverWorld];
}

// UIScene_CraftingMenu's constructor, once the movie is up
- (void)initCraftingMenu:(BOOL)table {
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
    mcle_container_open_crafting(table);
    MCLEContainerApplyOps();
}

// LocalPlayer's open for a container the server opened (openContainer and
// the rest), from handleContainerOpen
- (void)openServerContainer {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) {
        mcle_container_open_failed();
        return;
    }
    mcle_container_set_rect_reader(MCLEControlRectAt);
    self.menuStack = nil;
    [self transitionToMenuNamed:[NSString stringWithUTF8String:mcle_container_server_movie()]];
    if (!mcle_container_is_open()) mcle_container_open_failed();
    [self showContainerOverWorld];
}

// The server container's scene, once its movie is up, or its reloadMovie
// coming back from the keyboard
- (void)initServerContainer {
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
    if (g_container_reloading) mcle_container_reload();
    else mcle_container_open_server();
    MCLEContainerApplyOps();
}

// Minecraft::tick's crafting action in creative: app.LoadCreativeMenu
- (void)openCreativeMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    self.menuStack = nil;
    [self transitionToMenuNamed:@"CreativeMenu1080.swf"];
    [self showContainerOverWorld];
}

// UIScene_CreativeMenu's constructor, once the movie is up
- (void)initCreativeMenu {
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
    MCLECreativeLayout layout;
    layout.backgroundPanel = MCLEControlRectAt("MainPanel.BackgroundPanel");
    layout.containerList = MCLEControlRectAt("MainPanel.containerList");
    layout.hotbarList = MCLEControlRectAt("MainPanel.hotbarList");
    for (int i = 0; i < 8; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "MainPanel.TouchPanel_%d", i);
        layout.tabs[i] = MCLEControlRectAt(path);
    }
    layout.slider = MCLEControlRectAt("MainPanel.TouchPanel_Slider");
    mcle_container_open_creative(layout);
    MCLEContainerApplyOps();
}

// UIScene_InventoryMenu's constructor, once the movie is up
- (void)initInventoryMenu {
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
    MCLEInventoryLayout layout;
    layout.backgroundPanel = MCLEControlRectAt("MainPanel.BackgroundPanel");
    layout.hotbarList = MCLEControlRectAt("MainPanel.hotbarList");
    layout.inventoryList = MCLEControlRectAt("MainPanel.inventoryList");
    layout.armorList = MCLEControlRectAt("MainPanel.armorList");
    layout.player = MCLEControlRectAt("MainPanel.iggy_player");
    mcle_container_open_inventory(layout, CACurrentMediaTime());
    MCLEContainerApplyOps();
}

// handleDestroy, back to the HUD unless another scene is taking over
- (void)closeContainerMenu:(BOOL)backToGame {
    mcle_container_close();
    MCLEContainerApplyOps();
    g_world_container = false;
    g_world_info = false;
    if (backToGame) [self enterWorldUI];
}

// A UI frame of the container scene: the pad, UIScene::tick, then the
// mouse move the pointer makes and the movie with its slots drawn in
- (void)containerMenuFrame {
    extern PlayerHandle* g_ruffle_player;
    mcle_ios_pad_state pad = {};
    mcle_ios_input_poll(0, &pad);
    mcle_container_frame(pad.buttons, self.lastPadButtons, CACurrentMediaTime());
    self.lastPadButtons = pad.buttons;
    MCLEContainerApplyOps();
    if (mcle_container_close_requested()) {
        if (mcle_container_back_movie()) [self containerNavigateBack];
        else [self closeContainerMenu:YES];
        return;
    }
    if (const char* movie = mcle_container_take_navigate()) {
        [self containerNavigateTo:[NSString stringWithUTF8String:movie]];
        return;
    }
    int tips[11];
    mcle_container_tooltips(tips);
    MCLESetTooltips(tips);
    // The tutorial popup over the menu, and the slide its timers ask the
    // menu for
    MCLETutorialPopupFrame(true);
    MCLEContainerApplyOps();
    if (!g_ruffle_player) return;
    float px = 0, py = 0;
    if (mcle_container_pointer(&px, &py)) ruffle_ios_player_mouse_move_stage(g_ruffle_player, px, py);
    ruffle_ios_player_tick(g_ruffle_player, 1.0f / 60.0f);
    // The crafting menus have around 70 slots
    RuffleIosCustomDrawRegion regions[128];
    int n = ruffle_ios_custom_draw_regions(g_ruffle_player, regions, 128);
    n = std::max(0, std::min(n, 128));
    MCLETutorialPopupRegions();
    mcle_container_custom_draw(reinterpret_cast<const MCLECustomDrawRegion*>(regions), n);
}

// UIScene_DeathMenu: Respawn and Exit Game under "You died!"
- (void)openDeathMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    // Dying closes whatever menu was up
    if (g_world_container) [self closeContainerMenu:NO];
    g_world_dead = true;
    // The death menu is a menu to the game and the tutorial, and the
    // tutorial popup stays hidden while it's up
    mcle_game_set_menu_displayed(true);
    mcle_tutorial_show_popup(false);
    self.menuStack = nil;
    [self transitionToMenuNamed:@"DeathMenu1080.swf"];
}

- (void)initDeathMenu {
    extern PlayerHandle* g_ruffle_player;
    NSArray<NSDictionary*>* cfg = @[
        @{ @"name": @"Respawn",  @"label": MCLEStr(IDS_RESPAWN),   @"id": @(0) },
        @{ @"name": @"ExitGame", @"label": MCLEStr(IDS_EXIT_GAME), @"id": @(1) },
    ];
    [self applyMenuButtonConfig:cfg];
    MCLECallPath(@"Title", @"SetLabel", @[MCLEStr(IDS_YOU_DIED)]);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, -2, 0);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 1);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTooltipsDepth, 1);
}

// Longer Loading Screens on a level change's ConnectingProgress, which
// hides the tips in TU19: here it shows them like FullscreenProgress and
// is held from when the change started
static bool g_connecting_hold = false;
static void MCLEConnectingHoldStart(void) {
    if (g_connecting_hold || !mcle_settings_get_longer_loading()) return;
    g_connecting_hold = true;
    g_progress_tip_time = CACurrentMediaTime();
    g_loading_hold_start = g_progress_tip_time;
    MCLESetPathVisible(@"Tip", YES);
}

// ClientConnection::handleRespawn going to another dimension, and
// eAppAction_Respawn: UIScene_ConnectingProgress over the game, the
// FullscreenProgress movie with the panorama and logo and only the title
- (void)openConnectingProgress:(int)stringId respawn:(BOOL)respawn {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    NSString* title = stringId >= 0 ? MCLEStr(stringId) : @"";
    if (respawn) {
        g_connecting_respawn = true;
        g_respawns_seen = mcle_game_respawns();
    }
    if (g_world_connecting) {
        // The scene it navigates to over the last one only has a new title
        MCLECallPath(@"Title", @"SetLabel", @[title]);
        if (!respawn) MCLEConnectingHoldStart();
        return;
    }
    // 4J Stu - Fix for #13543 - Crash: Game crashes if entering a portal with the inventory menu open
    if (g_world_container) [self closeContainerMenu:NO];
    g_world_connecting = true;
    mcle_game_set_menu_displayed(true);
    if (!respawn) mcle_tutorial_show_popup(false);
    self.menuStack = nil;
    [self transitionToMenuNamed:@"FullscreenProgress1080.swf"];
    MCLECallPath(@"Title", @"SetLabel", @[title]);
    MCLESetPathVisible(@"ProgressBar", NO);
    MCLESetPathVisible(@"Tip", NO);
    g_connecting_hold = false;
    if (!respawn) MCLEConnectingHoldStart();
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, -2, 1);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 0);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kLogoDepth, 1);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTooltipsDepth, 1);
    const int none[11] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    MCLESetTooltips(none);
}

// eAppAction_WaitForDimensionChangeComplete / WaitForRespawnComplete:
// CloseUIScenes, back to the game
- (void)closeConnectingProgress {
    g_world_connecting = false;
    g_connecting_respawn = false;
    g_connecting_hold = false;
    if (g_world_dead && !mcle_player_is_dead()) g_world_dead = false;
    mcle_game_set_menu_displayed(false);
    // UIGroup::closeAllScenes lets the tutorial popup show again
    mcle_tutorial_show_popup(true);
    [self enterWorldUI];
}

// eAppAction_SaveGameCapturedThumbnail and AutosaveSaveGameCapturedThumbnail:
// FullscreenProgress with the tips, panorama and logo while
// SaveWorldThreadProc has the server save, the pad the scene's until then
- (void)openSaveProgress:(BOOL)autosave {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player || g_world_saving || g_exiting_world) return;
    const bool overPause = g_world_paused && [self.currentMenuSwf isEqualToString:@"PauseMenu1080.swf"];
    const bool overInfo = g_world_container && g_world_info &&
                          [self.currentMenuSwf isEqualToString:@"InGameInfoMenu1080.swf"];
    const bool overGame = !g_world_paused && !g_world_dead && !g_world_container && !g_world_connecting &&
                          !g_world_won;
    if (!overPause && !overInfo && !overGame) return;
    // Both reset the autosave timer
    MCLESetAutosaveTimerTime();
    // The autosave hides the tutorial popup until it's done. Before the save
    // starts, as it waits on the tick lock the save holds.
    if (autosave) mcle_tutorial_show_popup(false);
    // The scene's constructor clears the progress before its thread starts
    mcle_game_progress(1, NULL, NULL, NULL);
    if (!mcle_game_save(autosave ? 1 : 0)) {
        if (autosave) mcle_tutorial_show_popup(true);
        return;
    }
    g_world_saving = true;
    g_saving_autosave = autosave;
    g_saving_over = overPause ? 1 : (overInfo ? 2 : 0);
    // NavigateToScene's SetMenuDisplayed
    mcle_game_set_menu_displayed(true);
    if (overPause) {
        // NavigateBackToScene comes back to the pause menu
        [self navigateForwardTo:@"FullscreenProgress1080.swf"];
    } else {
        self.menuStack = nil;
        [self transitionToMenuNamed:@"FullscreenProgress1080.swf"];
    }
    g_progress_pct = 0;
    g_progress_title = -1;
    g_progress_status = -1;
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, -2, 1);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 0);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kLogoDepth, 1);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTooltipsDepth, 1);
    // updateTooltips: no select until done and no cancel
    const int none[11] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    MCLESetTooltips(none);
}

// UIScene_FullscreenProgress::tick over the save: the progress renderer's
// percent, title and status, the next tip, and once the server's save is
// done the completion, back to the pause menu (NavigateBackToScene) or to
// what the autosave went over (AutosaveNavigateBack)
- (void)saveProgressTick {
    int title = -1, status = -1, pct = 0;
    mcle_game_progress(0, &title, &status, &pct);
    if (pct < 0) pct = 0;
    if (pct != g_progress_pct) {
        g_progress_pct = pct;
        MCLECallPath(@"ProgressBar", @"setProgress", @[@(pct)]);
    }
    if (title >= 0 && title != g_progress_title) {
        g_progress_title = title;
        MCLECallPath(@"Title", @"SetLabel", @[MCLEStr(title)]);
    }
    if (status >= 0 && status != g_progress_status) {
        g_progress_status = status;
        MCLECallPath(@"ProgressBar", @"SetLabel", @[MCLEStr(status)]);
    }
    MCLEProgressTipTick();
    if (!mcle_game_save_done()) return;
    g_world_saving = false;
    if (g_saving_over == 1) {
        [self navigateBack];
        return;
    }
    // This just allows it to be shown
    if (g_saving_autosave) mcle_tutorial_show_popup(true);
    if (g_saving_over == 2) {
        // The info menu's movie back up for its reloadMovie
        g_container_reloading = true;
        [self transitionToMenuNamed:@"InGameInfoMenu1080.swf"];
        g_container_reloading = false;
        [self showContainerOverWorld];
    } else {
        [self enterWorldUI];
    }
}

// ClientConnection::handleGameEvent WIN_GAME: UIScene_EndPoem, fullscreen
// over the game
- (void)openEndPoem {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    if (g_world_container) [self closeContainerMenu:NO];
    g_world_won = true;
    mcle_game_set_menu_displayed(true);
    mcle_tutorial_show_popup(false);
    self.menuStack = nil;
    [self transitionToMenuNamed:@"EndPoem1080.swf"];
}

// UIScene_EndPoem constructor
- (void)initEndPoem {
    extern PlayerHandle* g_ruffle_player;
    self.menuButtonConfig = nil;
    g_endpoem_ignore_input = false;
    mcle_end_poem_init();
    g_endpoem_requested = 0;
    // The poem's dirt and shadow fill the whole 1920x1080 movie, all of
    // TU19's 16:9 screen, and the text scrolls up its middle. Like the
    // credits background it's grown about the centre to fill a wider
    // screen, so the game doesn't show at the sides.
    const float s = MCLEMovieCoverScale(self.view.bounds.size);
    ruffle_ios_set_root_transform(g_ruffle_player, s, s, 960.0f * (1.0f - s), 540.0f * (1.0f - s));
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, -2, 0);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 0);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTooltipsDepth, 1);
}

- (void)resumeFromPause {
    g_world_paused = false;
    // ~UIScene_PauseMenu lets the tutorial popup show again
    mcle_tutorial_show_popup(true);
    mcle_game_set_paused(0);
    [self enterWorldUI];
}

- (void)enterWorldUI {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;
    // The menus are gone once the game starts, UIScene_HUD takes over.
    // CloseUIScenes' SetMenuDisplayed, the game gets the pad back once
    // CheckMenuDisplayed has counted the frames down.
    mcle_game_set_menu_displayed(false);
    self.menuStack = nil;
    [self transitionToMenuNamed:@"HUD1080.swf"];
    MCLEHudReset();
    MCLEGameTooltipsReset();
    const int depths[] = { -2, kMenuBackgroundDepth, kLogoDepth, kDimDepth, kTooltipsDepth };
    for (int d : depths) ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, d, 0);
    if (self.splashLabel) self.splashLabel.hidden = YES;
    // In game the upstream SoundEngine plays the music
    mcle_audio_stop_menu_music();
}

// UIScene_FullscreenProgress::tick, fed from the iOS init stages instead
// of Minecraft::progressRenderer.
- (void)pollWorldLoad {
    int pct = 0;
    int st = mcle_game_load_status(&pct);
    if (st < 0) {
        g_loading_world = false;
        MCLELoadingHoldPause(false);
        __weak MinecraftViewController* weakSelf = self;
        [self presentDialogWithTitle:MCLEStr(IDS_CORRUPT_OR_DAMAGED_SAVE_TITLE)
                             content:@""
                             buttons:@[MCLEStr(IDS_CONFIRM_OK)]
                          focusIndex:0
                            callback:^(MCLEDialogResult r) {
            [weakSelf navigateBack];
        }];
        return;
    }
    // A new world's findSeed has a stage of its own while the server starts
    bool findingSeed = st == 0 && mcle_game_load_finding_seed();
    int stage = findingSeed ? 10 : st;
    if (stage != g_progress_stage) {
        if (g_progress_stage == -2)
            MCLECallPath(@"Title", @"SetLabel", @[MCLEStr(IDS_PROGRESS_INITIALISING_SERVER)]);
        g_progress_stage = stage;
        // loadLevel generates the spawn area of a new level
        int spawnId = mcle_game_load_is_new() ? IDS_PROGRESS_GENERATING_SPAWN_AREA : IDS_PROGRESS_LOADING_SPAWN_AREA;
        int statusId = findingSeed ? IDS_PROGRESS_NEW_WORLD_SEED
                     : (st == 1) ? spawnId
                     : (st >= 2) ? IDS_PROGRESS_SAVING_TO_DISC : 0;
        MCLECallPath(@"ProgressBar", @"SetLabel", @[statusId ? MCLEStr(statusId) : @""]);
    }
    if (pct != g_progress_pct) {
        g_progress_pct = pct;
        MCLECallPath(@"ProgressBar", @"setProgress", @[@(pct)]);
    }
    MCLEProgressTipTick();
}

// UIScene_PauseMenu / UIScene_DeathMenu Exit Game for the host with saving
// on: RequestAlertMessage(IDS_EXIT_GAME, IDS_CONFIRM_EXIT_GAME) with Cancel,
// Save and Exit, Exit without Saving
- (void)requestExitGame {
    __weak MinecraftViewController* weakSelf = self;
    [self presentDialogWithTitle:MCLEStr(IDS_EXIT_GAME)
                         content:MCLEStr(IDS_CONFIRM_EXIT_GAME)
                         buttons:@[MCLEStr(IDS_CONFIRM_CANCEL), MCLEStr(IDS_EXIT_GAME_SAVE),
                                   MCLEStr(IDS_EXIT_GAME_NO_SAVE)]
                      focusIndex:0
                        callback:^(MCLEDialogResult r) {
        [weakSelf exitGameSaveDialogReturned:r];
    }];
}

// IUIScene_PauseMenu::ExitGameSaveDialogReturned. The save is there, so
// saving asks before overwriting it, and not saving asks too.
- (void)exitGameSaveDialogReturned:(MCLEDialogResult)r {
    __weak MinecraftViewController* weakSelf = self;
    if (r == MCLEDialogResultDecline) {
        [self presentDialogWithTitle:MCLEStr(IDS_TITLE_SAVE_GAME)
                             content:MCLEStr(IDS_CONFIRM_SAVE_GAME)
                             buttons:@[MCLEStr(IDS_CONFIRM_CANCEL), MCLEStr(IDS_CONFIRM_OK)]
                          focusIndex:0
                            callback:^(MCLEDialogResult r2) {
            [weakSelf exitGameConfirmReturned:r2 saving:YES];
        }];
    } else if (r == MCLEDialogResultThird) {
        [self presentDialogWithTitle:MCLEStr(IDS_TITLE_DECLINE_SAVE_GAME)
                             content:MCLEStr(IDS_CONFIRM_DECLINE_SAVE_GAME)
                             buttons:@[MCLEStr(IDS_CONFIRM_CANCEL), MCLEStr(IDS_CONFIRM_OK)]
                          focusIndex:0
                            callback:^(MCLEDialogResult r2) {
            [weakSelf exitGameConfirmReturned:r2 saving:NO];
        }];
    }
}

// ExitGameAndSaveReturned / ExitGameDeclineSaveReturned: OK leaves, anything
// else goes back to the exit choices if the pause menu is up
- (void)exitGameConfirmReturned:(MCLEDialogResult)r saving:(BOOL)save {
    if (r == MCLEDialogResultDecline) {
        [self exitWorldSaving:save];
    } else if ([self.currentMenuSwf isEqualToString:@"PauseMenu1080.swf"]) {
        [self requestExitGame];
    }
}

// eAppAction_ExitWorld then ExitWorldCapturedThumbnail: FullscreenProgress
// with "Exiting game", the tips, panorama and logo while
// ExitWorldThreadProc runs
- (void)exitWorldSaving:(BOOL)save {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player || g_exiting_world) return;
    // The save on the way out has the frame from before leaving
    if (save) {
        __weak MinecraftViewController* weakSelf = self;
        MCLECaptureThumbnailThen(^{ [weakSelf exitWorldAfterThumbnailSaving:YES]; });
        return;
    }
    [self exitWorldAfterThumbnailSaving:NO];
}

- (void)exitWorldAfterThumbnailSaving:(BOOL)save {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player || g_exiting_world) return;
    if (!mcle_game_exit_world(save ? 1 : 0)) return;
    g_exiting_world = true;
    // clear the autosave timer that might be on screen
    MCLEShowAutosaveCountdownTimer(false);
    self.menuStack = nil;
    [self transitionToMenuNamed:@"FullscreenProgress1080.swf"];
    MCLECallPath(@"Title", @"SetLabel", @[MCLEStr(IDS_EXITING_GAME)]);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, -2, 1);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 0);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kLogoDepth, 1);
    ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTooltipsDepth, 1);
}

// e_ProgressCompletion_NavigateToHomeMenu once the world is gone
- (void)finishExitWorld {
    extern PlayerHandle* g_ruffle_player;
    g_exiting_world = false;
    g_world_paused = false;
    g_world_dead = false;
    g_world_connecting = false;
    g_connecting_respawn = false;
    g_world_won = false;
    self.menuStack = nil;
    // UIController::NavigateToHomeMenu: the menus are on the default texture
    // pack, and the home menu comes once its skin is back
    // (m_navigateToHomeOnReload)
    mcle_texture_packs_navigate_to_home_menu();
    mcle_handle_xui_actions();
    if (mcle_ui_take_skin_reload()) MCLEReloadSkinLibraries();
    [self transitionToMenuNamed:@"MainMenu1080.swf"];
    if (g_ruffle_player) {
        ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, -2, 1);
        ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kMenuBackgroundDepth, 0);
        ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kTooltipsDepth, 1);
    }
    // The menus have their music back
    mcle_audio_start_menu_music();
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
    // Labels mirror IDS_HOW_TO_PLAY_MENU_* in strings.resx
    // (lines 504-691). Ordering + ids match the console enum
    // in UIScene_HowToPlayMenu.cpp:6-39 so when per-topic scenes
    // land the press ids line up with console's handlePress.
    struct Item { const char* label; int id; };
    Item items[] = {
        { "What's New",              0 },
        { "Basics",                  1 },
        { "Multiplayer",             2 },
        { "HUD",                     3 },
        { "Creative Mode",           4 },
        { "Inventory",               5 },
        { "Chests",                  6 },
        { "Crafting",                7 },
        { "Furnace",                 8 },
        { "Dispenser",               9 },
        { "Brewing",                10 },
        { "Enchantment",            11 },
        { "Anvil",                  12 },
        { "Farming Animals",        13 },
        { "Breeding Animals",       14 },
        { "Trading",                15 },
        { "Horses",                 16 },
        { "Beacons",                17 },
        { "Fireworks",              18 },
        { "Hoppers",                19 },
        { "Droppers",               20 },
        { "Nether Portal",          21 },
        { "The End",                22 },
        { "Host and Player Options",23 },
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
        @{ @"name": @"Button1", @"label": MCLEStr(IDS_OPTIONS),           @"id": @(0) },
        @{ @"name": @"Button2", @"label": MCLEStr(IDS_AUDIO),             @"id": @(1) },
        @{ @"name": @"Button3", @"label": MCLEStr(IDS_CONTROL),           @"id": @(2) },
        @{ @"name": @"Button4", @"label": MCLEStr(IDS_GRAPHICS),          @"id": @(4) },
        @{ @"name": @"Button5", @"label": MCLEStr(IDS_USER_INTERFACE),    @"id": @(5) },
        @{ @"name": @"Button6", @"label": MCLEStr(IDS_RESET_TO_DEFAULTS), @"id": @(6) },
    ];
    [self attachMenuScenery];
    [self applyMenuButtonConfig:cfg];
}

// UIScene_LeaderboardsMenu constructor. Windows64's LeaderboardManager
// never answers a read (WindowsLeaderboardManager.h), so OnStatsReadComplete
// never comes and the scene stays on the loading message with m_bIgnoreInput
// set.
- (void)initLeaderboardMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    // m_currentLeaderboard 0, m_currentDifficulty 2: SetLeaderboardHeader
    MCLECallPath(@"Leaderboard", @"SetLabel", @[MCLEStr(IDS_LEADERBOARD_TRAVELLING_NORMAL)]);
    // eFM_Friends
    MCLECallPath(@"Filter", @"Init", @[[MCLEStr(IDS_LEADERBOARD_FILTER)
                                          stringByAppendingString:MCLEStr(IDS_LEADERBOARD_FILTER_FRIENDS)]]);
    MCLECallPath(@"Entries", @"Init", @[[NSString stringWithFormat:@"%@%i", MCLEStr(IDS_LEADERBOARD_ENTRIES), 0]]);
    // ReadStats(-1): m_listEntries.clearList, then the loading message. The
    // movie's constructor moved Info onto the stage, its property finds it.
    MCLECallPath(@"Gamers", @"ResetLeaderboard", @[]);
    MCLECallPath(@"Info", @"SetLabel", @[MCLEStr(IDS_LEADERBOARD_LOADING)]);
    MCLESetPathVisible(@"Info", YES);

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// UIScene_DLCMainMenu constructor. The offer categories are only added on
// Durango (and from the PS store in tick), so on Windows64 the list stays
// empty under its title.
- (void)initDLCMainMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];

    MCLECallPath(@"OffersList_Title", @"Init", @[MCLEStr(IDS_DOWNLOADABLE_CONTENT_OFFERS)]);
    // eControl_OffersList
    const char* list = "OffersList";
    ruffle_ios_call_list_init(g_ruffle_player,
        (const uint8_t*)list, strlen(list), 0.0);

    ruffle_ios_call_root_set_focus(g_ruffle_player, -1.0);

    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;
}

// UIScene_SkinSelectMenu's constructor, once the movie is up. The logo is
// already off for the scene (applyLogoVisibilityForScene).
- (void)initSkinSelectMenu {
    extern PlayerHandle* g_ruffle_player;
    if (!g_ruffle_player) return;

    [self attachMenuScenery];
    self.menuButtonConfig = nil;
    self.menuFocusIndex = 0;

    mcle_skin_select_open(CACurrentMediaTime());
    MCLEContainerApplyOps();
    int tips[11];
    mcle_container_tooltips(tips);
    MCLESetTooltips(tips);
}

// The scene's handleDestroy, and its characters off the UI layer
- (void)closeSkinSelectMenu {
    mcle_skin_select_close();
    mcle_hud_custom_draw(NULL, 0);
}

// A UI frame of the skin select menu: the pad (UIController::handleInput),
// the scene's tick, then the movie with the characters drawn in
- (void)skinSelectMenuFrame {
    extern PlayerHandle* g_ruffle_player;
    mcle_ios_pad_state pad = {};
    mcle_ios_input_poll(0, &pad);
    mcle_skin_select_frame(pad.buttons, self.lastPadButtons, CACurrentMediaTime());
    self.lastPadButtons = pad.buttons;
    MCLEContainerApplyOps();
    if (mcle_skin_select_close_requested()) {
        [self navigateBack];
        return;
    }
    int tips[11];
    mcle_container_tooltips(tips);
    MCLESetTooltips(tips);
    ruffle_ios_player_tick(g_ruffle_player, 1.0f / 60.0f);
    RuffleIosCustomDrawRegion regions[16];
    int n = ruffle_ios_custom_draw_regions(g_ruffle_player, regions, 16);
    n = std::max(0, std::min(n, 16));
    mcle_skin_select_custom_draw(reinterpret_cast<const MCLECustomDrawRegion*>(regions), n);
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
    // MessageBox.cpp:49. Dim sits ABOVE the logo (kDimDepth >
    // kLogoDepth) so logo + scene are darkened underneath it,
    // matching console where the dialog overlay covers everything
    // except the tooltips strip.
    ruffle_ios_set_xui_sibling_visible_at_depth(
        g_ruffle_player, kDimDepth, 1);

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
        ruffle_ios_set_xui_sibling_visible_at_depth(g_ruffle_player, kDimDepth, 0);
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
//   [tick burst so TextField layout completes]
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

    // Title / Content labels. FJ_Label.Init takes a single arg
    // (the label text) -- passing (text, id) causes
    // ArgumentError #1063. Use SetLabel, which the class
    // explicitly supports as a post-construction update path
    // that also flips m_bInitialised.
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

    // Tick the player a few frames so the TextField layout
    // triggered by each SetLabel (text wrap, line-break pass,
    // textHeight measure) completes BEFORE AutoResize reads back
    // Content's current height. Without these ticks, Content's
    // textHeight on longer bodies (e.g. Reset Settings wraps to
    // 3 lines) reflects the authored placeholder height, which
    // makes the panel over-tall and leaves a visible gap between
    // the body text and the button row.
    for (int i = 0; i < 10; ++i) {
        ruffle_ios_player_tick_headless(g_ruffle_player, 1.0f / 60.0f);
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
        // FJ_Document. Logo wasn't toggled on dialog open (the
        // dim covers it underneath), so no logo restore needed.
        ruffle_ios_set_xui_sibling_visible_at_depth(
            g_ruffle_player, kDimDepth, 0);
        ruffle_ios_remove_sibling_at_depth(
            g_ruffle_player, kMessageBoxDepth);
        ruffle_ios_clear_stage_dispatch_list(g_ruffle_player);
        ruffle_ios_redispatch_added_to_stage(g_ruffle_player, -1);
        // Restore the underlying scene's tooltips and focus
        // highlight. seedTooltipsForScene picks the (A, B) pair
        // that scene's updateTooltips override would have set
        // on console; refreshFocusState pushes ChangeState=
        // SELECTED on whichever button menuFocusIndex points at.
        if (self.currentMenuSwf.length) {
            [self seedTooltipsForScene:self.currentMenuSwf hidden:NO];
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
        if (mcle_skin_select_is_open()) [self closeSkinSelectMenu];
        if (mcle_keyboard_front_end_is_open()) [self closeFrontEndKeyboard];
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
        // UIScene::initialiseMovie -> updateSafeZone, fullscreen
        MCLECallPath(@"", @"SetSafeZone", @[@(kSafeZoneTop), @(kSafeZoneBottom), @(kSafeZoneLeft), @(kSafeZoneRight)]);
        if ([swfName isEqualToString:@"MainMenu1080.swf"]) {
            [self initMainMenuButtons];
        } else if ([swfName isEqualToString:@"SaveMessage1080.swf"]) {
            [self initSaveMessage];
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
        } else if ([swfName isEqualToString:@"LoadMenu1080.swf"]) {
            [self initLoadMenu];
        } else if ([swfName isEqualToString:@"CreateWorldMenu1080.swf"]) {
            [self initCreateWorldMenu];
        } else if ([swfName isEqualToString:@"LaunchMoreOptionsMenu1080.swf"]) {
            [self initMoreOptionsMenu];
        } else if ([swfName isEqualToString:@"FullscreenProgress1080.swf"]) {
            [self initFullscreenProgress];
        } else if ([swfName isEqualToString:@"PauseMenu1080.swf"]) {
            [self initPauseMenu];
        } else if ([swfName isEqualToString:@"HUD1080.swf"]) {
            // UIScene_HUD constructor
            MCLECallPath(@"", @"SetDragonLabel", @[MCLEStr(IDS_BOSS_ENDERDRAGON_HEALTH)]);
            MCLECallPath(@"", @"SetSelectedLabel", @[@""]);
            for (int i = 1; i <= 10; ++i) {
                MCLECallPath([NSString stringWithFormat:@"Label%d", i], @"Init", @[@""]);
            }
            MCLECallPath(@"Jukebox", @"Init", @[@""]);
        } else if ([swfName isEqualToString:@"DeathMenu1080.swf"]) {
            [self initDeathMenu];
        } else if ([swfName isEqualToString:@"EndPoem1080.swf"]) {
            [self initEndPoem];
        } else if ([swfName isEqualToString:@"HowToPlayMenu1080.swf"]) {
            [self initHowToPlayMenu];
        } else if ([swfName isEqualToString:@"Credits1080.swf"]) {
            MCLECreditsInit();
            MCLECreditsCoverScreen(g_ruffle_player, self.view.bounds.size);
        } else if ([swfName isEqualToString:@"HowToPlay1080.swf"]) {
            [self attachMenuScenery];
            self.menuButtonConfig = nil;
            MCLEHowToPlayInit();
        } else if ([swfName isEqualToString:@"Controls1080.swf"]) {
            [self attachMenuScenery];
            self.menuButtonConfig = nil;
            MCLEControlsInit();
        } else if ([swfName isEqualToString:@"InventoryMenu1080.swf"]) {
            [self initInventoryMenu];
        } else if ([swfName isEqualToString:@"Crafting2x2Menu1080.swf"]) {
            [self initCraftingMenu:NO];
        } else if ([swfName isEqualToString:@"Crafting3x3Menu1080.swf"]) {
            [self initCraftingMenu:YES];
        } else if ([swfName isEqualToString:@"CreativeMenu1080.swf"]) {
            [self initCreativeMenu];
        } else if ([swfName isEqualToString:[NSString stringWithUTF8String:mcle_container_server_movie()]]) {
            [self initServerContainer];
        } else if ([swfName isEqualToString:@"SignEntryMenu1080.swf"]) {
            [self initSignEntryMenu];
        } else if ([swfName isEqualToString:@"Keyboard1080.swf"]) {
            [self initKeyboardMenu];
        } else if ([swfName isEqualToString:@"InGameInfoMenu1080.swf"]) {
            [self initInGameInfoMenu];
        } else if ([swfName isEqualToString:@"InGameHostOptions1080.swf"] ||
                   [swfName isEqualToString:@"InGamePlayerOptions1080.swf"]) {
            [self initInGameOptionsMenu];
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
        // UIScene_DLCMainMenu has none, the logo stays as the main
        // menu showed it.
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
    // The main loop's app.HandleXuiActions, the texture pack reload
    mcle_handle_xui_actions();
    if (mcle_ui_take_skin_reload()) MCLEReloadSkinLibraries();

    mcle_game_tick();

    // UIController::CheckMenuDisplayed, once a frame
    mcle_game_check_menu_displayed();

    // The thumbnail came back from the world frame drawn last tick (or no
    // world frame came), so what was waiting on it goes
    if (g_after_thumbnail &&
        (!mcle_metal_thumbnail_pending() || ++g_after_thumbnail_frames > 30)) {
        void (^then)(void) = g_after_thumbnail;
        g_after_thumbnail = nil;
        then();
    }


    // GameRenderer::CachePlayerGammas / ComputeGammaFromSlider. Windows64's
    // PostProcesser puts it on the whole frame after ui.render, so the menus
    // and HUD get it too, and outside a world it comes from the slider alone.
    {
        float slider = (float)mcle_settings_get(MCLE_SETTING_Gamma);
        slider = std::max(slider, 0.0f);
        slider = std::min(slider, 100.0f);
        float gamma = slider > 50.0f ? 1.0f + (slider - 50.0f) / 50.0f * 1.2f
                                     : 1.0f - (50.0f - slider) / 50.0f * 0.4f;
        mcle_metal_set_gamma(gamma);
        extern PlayerHandle* g_ruffle_player;
        static PlayerHandle* s_gammaPlayer = NULL;
        static float s_ruffleGamma = 1.0f;
        if (g_ruffle_player && (g_ruffle_player != s_gammaPlayer || gamma != s_ruffleGamma)) {
            ruffle_ios_player_set_gamma(g_ruffle_player, gamma);
            s_gammaPlayer = g_ruffle_player;
            s_ruffleGamma = gamma;
        }
    }

    // Phase G phase 1A: once the world simulation is ticking, take over
    // the screen with the legacy Metal path (sky-color clear via
    // mcle_render_frame). Bypasses Ruffle entirely so the SWF menu
    // doesn't keep drawing on top of the world output.
    MCLEPanoramaTick();

    // Longer Loading Screens: the world stays paused under its loading
    // screen until the hold is over, then starts as it would have
    bool loadHeld = false;
    if (g_loading_world) {
        loadHeld = MCLELoadingHeld();
        MCLELoadingHoldPause(loadHeld);
    }

    if (mcle_world_is_ticking() && !loadHeld) {
        if (self.splashLabel) self.splashLabel.hidden = YES;
        if (g_loading_world) {
            g_loading_world = false;
            NSLog(@"[MinecraftVC] world ready, leaving FullscreenProgress");
            [self enterWorldUI];
            // UIScene_LoadMenu starts the autosave timer with the game
            MCLESetAutosaveTimerTime();
            // Level changes and wins from here on are this world's
            g_level_changes_seen = mcle_game_level_changes(NULL, NULL);
            g_wins_seen = mcle_game_wins();
        }
        if (g_world_saving) [self saveProgressTick];
        // ClientConnection::handleRespawn and handleGameEvent's WIN_GAME
        {
            int finished = 0, titleId = -1;
            const int started = mcle_game_level_changes(&finished, &titleId);
            if (started != g_level_changes_seen && !g_world_saving) {
                g_level_changes_seen = started;
                [self openConnectingProgress:titleId respawn:NO];
            }
            const int wins = mcle_game_wins();
            if (wins != g_wins_seen && !g_world_saving) {
                g_wins_seen = wins;
                [self openEndPoem];
            }
            // eAppAction_WaitForDimensionChangeComplete / WaitForRespawnComplete
            const bool connected = g_world_connecting && finished == g_level_changes_seen &&
                                   (!g_connecting_respawn || mcle_game_respawns() != g_respawns_seen);
            // Longer Loading Screens: once the change is done the game waits
            // paused under the screen for the rest of the hold
            const bool connectHeld = connected && g_connecting_hold && MCLELoadingHeld();
            MCLELoadingHoldPause(connectHeld);
            if (connected && !connectHeld) {
                [self closeConnectingProgress];
            } else if (g_world_connecting && g_connecting_hold) {
                MCLEProgressTipTick();
            }
        }
        // Minecraft::run's autosave, with no app action going (a level change,
        // a thumbnail being taken): not with the pause menu or a scene that
        // ignores autosaves up (death, containers, the End poem,
        // FullscreenProgress), and the timer starts again once the save has
        // gone. The last 5 seconds count down. The host and player options
        // over the info menu have no reloadMovie to come back with, so it
        // waits for them to go.
        if (!g_world_connecting && !g_after_thumbnail) {
            const bool infoUp = g_world_container && g_world_info &&
                                [self.currentMenuSwf isEqualToString:@"InGameInfoMenu1080.swf"];
            if (!g_world_paused && !g_world_dead && (!g_world_container || infoUp) && !g_world_won &&
                !g_world_saving) {
                unsigned char autosave = mcle_settings_get(MCLE_SETTING_Autosave);
                const CFTimeInterval now = CACurrentMediaTime();
                if (autosave != 0 && now > g_autosave_due) {
                    // disable the autosave countdown
                    MCLEShowAutosaveCountdownTimer(false);
                    NSLog(@"[MinecraftVC] autosave");
                    __weak MinecraftViewController* weakSelf = self;
                    MCLECaptureThumbnailThen(^{ [weakSelf openSaveProgress:YES]; });
                    MCLESetAutosaveTimerTime();
                } else if (autosave != 0) {
                    const unsigned int secs = (unsigned int)(g_autosave_due - now);
                    if (secs < 6) {
                        MCLEShowAutosaveCountdownTimer(true);
                        MCLEUpdateAutosaveCountdownTimer(secs);
                    }
                }
            } else {
                // disable the autosave countdown
                MCLEShowAutosaveCountdownTimer(false);
            }
        }
        // Minecraft::tick shows the death menu once the player has died with
        // no menu up (the in game info menu holds it off), and goes back to
        // the game when the respawned player is alive
        if (!g_world_dead && !g_world_paused && !g_world_connecting && !g_world_won && !g_world_saving &&
            !(g_world_container && g_world_info) && mcle_player_is_dead()) {
            [self openDeathMenu];
        } else if (g_world_dead && !mcle_player_is_dead()) {
            g_world_dead = false;
            // ~UIScene_DeathMenu, unless ConnectingProgress took its place
            if (!g_world_connecting) {
                mcle_game_set_menu_displayed(false);
                mcle_tutorial_show_popup(true);
                [self enterWorldUI];
            }
        }
        // Minecraft::tick: the game info action opens the in game info menu,
        // Start the pause menu, the inventory action the inventory
        const bool sceneUp = g_world_connecting || g_world_won || g_world_saving;
        if (!g_world_paused && !g_world_dead && !g_world_container && !sceneUp) {
            mcle_ios_pad_state pad;
            if (mcle_ios_input_poll(0, &pad)) {
                // InputManager.ButtonPressed gives no game actions while a
                // menu is still displayed
                const uint32_t game = mcle_game_menu_displayed() ? 0 : pad.buttons;
                bool startNow = (game & 0x00000010u) != 0;
                bool startBefore = (self.lastPadButtons & 0x00000010u) != 0;
                bool inventoryPressed = (game & ~self.lastPadButtons & mcle_container_inventory_buttons()) != 0;
                bool gameInfoPressed = (game & ~self.lastPadButtons & mcle_ingame_info_buttons()) != 0;
                self.lastPadButtons = pad.buttons;
                if (gameInfoPressed && mcle_ingame_info_input_allowed()) [self openInGameInfoMenu];
                else if (startNow && !startBefore) [self openPauseMenu];
                else if (inventoryPressed) [self openInventoryMenu];
            }
        }
        // The crafting action and the server's ContainerOpenPacket
        if (!g_world_paused && !g_world_dead && !sceneUp) {
            int request = mcle_container_take_request();
            if (request == MCLE_CONTAINER_CRAFTING_2X2) [self openCraftingMenu:NO];
            else if (request == MCLE_CONTAINER_CRAFTING_3X3) [self openCraftingMenu:YES];
            else if (request == MCLE_CONTAINER_CREATIVE) [self openCreativeMenu];
            else if (request == MCLE_CONTAINER_SERVER) [self openServerContainer];
            // ClientConnection::handleTileEditorOpen
            if (mcle_sign_entry_take_request()) [self openSignEntryMenu];
        }
        // Saving, the game isn't started and the world isn't drawn
        if (!g_world_saving) mcle_render_frame();
        if (g_world_container && !g_world_saving) {
            [self containerMenuFrame];
            return;
        }
        if (!g_world_paused && !g_world_dead && !sceneUp) {
            const CFTimeInterval uiStart = CACurrentMediaTime();
            MCLEHudPump();
            MCLEGameTooltipsPump();
            MCLETutorialPopupFrame(true);
            // Keep the SWF layer presenting over the world (transparent stage)
            extern PlayerHandle* g_ruffle_player;
            if (g_ruffle_player) {
                ruffle_ios_player_tick(g_ruffle_player, 1.0f / 60.0f);
                // The custom draw callbacks for what the movie just drew
                static_assert(sizeof(RuffleIosCustomDrawRegion) == sizeof(MCLECustomDrawRegion),
                              "region layouts differ");
                RuffleIosCustomDrawRegion regions[32];
                int n = ruffle_ios_custom_draw_regions(g_ruffle_player, regions, 32);
                n = std::max(0, std::min(n, 32));
                MCLETutorialPopupRegions();
                mcle_hud_custom_draw(reinterpret_cast<const MCLECustomDrawRegion*>(regions), n);
            }
            mcle_metal_add_phase_ms(2, (CACurrentMediaTime() - uiStart) * 1000.0);
            return;
        }
        // The HUD isn't up, nothing to draw over it. Change Skin draws its
        // characters on the same layer.
        if (!mcle_skin_select_is_open()) mcle_hud_custom_draw(NULL, 0);
        // Paused: the pause menu takes input below like any other menu,
        // drawn over the frozen world
    }
    // Not over the HUD or a menu in game, so the tutorial popup is hidden
    MCLETutorialPopupFrame(false);

    if (g_loading_world) {
        // A world that's ready leaves the last progress up, the tips go on
        if (loadHeld && mcle_world_is_ticking()) MCLEProgressTipTick();
        else [self pollWorldLoad];
    }
    if (g_exiting_world && mcle_game_exit_world_done()) [self finishExitWorld];

    // Splash text pulse + position. Mirrors CXuiCtrlSplashPulser::
    // OnRender (Common/XUI/XUI_Ctrl_SplashPulser.cpp): pulses with
    // sin(currentTimeMillis), rotated -17 degrees, centered at the
    // authored (612, 126) position on the 1920x1080 stage. Only
    // visible on MainMenu; hidden on sub-scenes to match console.
    if (self.splashLabel) {
        BOOL onMainMenu = [self.currentMenuSwf isEqualToString:@"MainMenu1080.swf"];
        self.splashLabel.hidden = !onMainMenu;
        // Splash is a UIKit UILabel painted on top of the wgpu
        // surface, so the in-engine dim layer can't reach it.
        // Alpha-dim it manually while a dialog is up to mirror
        // what the dim does to the rest of the scene underneath.
        // MenuBackground1080.swf's authored fill is a black box
        // at roughly alpha 0.65, which darkens the scene below it
        // to ~35% of original brightness. Aim for similar: alpha
        // 0.2 lets enough yellow through that the splash is still
        // visibly there, but muted to about the same level as the
        // panorama / logo / buttons behind the dialog.
        self.splashLabel.alpha = g_pending_dialog ? 0.2 : 1.0;
        if (onMainMenu) {
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

    if ([self.currentMenuSwf isEqualToString:@"Credits1080.swf"]) MCLECreditsTick();
    if ([self.currentMenuSwf isEqualToString:@"EndPoem1080.swf"]) {
        MCLEEndPoemInput();
        MCLEEndPoemTick();
    }
    // UIScene_ControlsMenu::tick
    if ([self.currentMenuSwf isEqualToString:@"Controls1080.swf"] && g_controls_layout_changed) {
        MCLEControlsPositionAllText();
    }

    // UIScene_SkinSelectMenu takes the pad itself, the way UIController
    // hands it to the scene
    if (g_ruffle_player && mcle_skin_select_is_open() && !g_pending_dialog) {
        [self skinSelectMenuFrame];
        return;
    }
    // and so does UIScene_Keyboard over a front end menu
    if (g_ruffle_player && mcle_keyboard_front_end_is_open() && !g_pending_dialog) {
        [self frontEndKeyboardFrame];
        return;
    }

    // Edge-detect controller buttons and forward press/release events to
    // Ruffle so the menu's AS3 input handlers fire. Bit layout matches
    // 4J_Input.h; the int code we pass matches ruffle_ios.h's mapping.
    if (g_ruffle_player) {
        mcle_ios_pad_state pad;
        if (mcle_ios_input_poll(0, &pad)) {
            // iggy is the key UIScene::convertGameActionToIggyKeycode
            // gives the button's menu action, -1 for none
            static const struct { uint32_t mask; int code; int iggy; } btnMap[] = {
                { 0x00000001u,  0,  13 },  // A     -> South, ENTER
                { 0x00000002u,  1,  27 },  // B     -> East, ESCAPE
                { 0x00000008u,  2, 113 },  // Y     -> North, F2
                { 0x00000004u,  3, 112 },  // X     -> West, F1
                { 0x00000010u,  4,  -1 },  // START
                { 0x00000020u,  5,  -1 },  // BACK  -> Select
                { 0x00000400u,  6,  38 },  // DPadUp, UP
                { 0x00000800u,  7,  40 },  // DPadDown, DOWN
                { 0x00001000u,  8,  37 },  // DPadLeft, LEFT
                { 0x00002000u,  9,  39 },  // DPadRight, RIGHT
                { 0x00000080u, 10, 115 },  // LB    -> LeftTrigger, F4
                { 0x00000040u, 11, 114 },  // RB    -> RightTrigger, F3
                { 0x00800000u, 12,  33 },  // LT    -> LeftTrigger2, PAGE_UP
                { 0x00400000u, 13,  34 },  // RT    -> RightTrigger2, PAGE_DOWN
            };
            uint32_t now = pad.buttons;
            uint32_t prev = self.lastPadButtons;
            uint32_t changed = now ^ prev;
            // UIScene_LeaderboardsMenu::handleInput only takes
            // ACTION_MENU_CANCEL while m_bIgnoreInput is set, which on
            // Windows64 it always is
            const bool cancelOnly = !g_pending_dialog &&
                [self.currentMenuSwf isEqualToString:@"LeaderboardMenu1080.swf"];
            for (size_t i = 0; i < sizeof(btnMap)/sizeof(btnMap[0]); ++i) {
                if (changed & btnMap[i].mask) {
                    if (now & btnMap[i].mask) {
                        if (cancelOnly && btnMap[i].mask != 0x00000002u) continue;
                        // The scene's handleInput animates the press first
                        MCLEAnimateKeyPress(btnMap[i].iggy);
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
                            // 4J-PB - Need to check for installed DLC
                            mcle_dlc_install_if_needed(0);
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
                            // UIScene_MainMenu's eControl_Exit:
                            // RequestErrorMessage(IDS_WINDOWS_EXIT,
                            // IDS_WARNING_ARCADE_TEXT) with Cancel then
                            // OK, focus on Cancel. ExitGameReturned has
                            // the buttons reversed, OK is the Decline
                            // that runs app.ExitGame. Small delay so the
                            // SFX plays before the process goes.
                            [self presentDialogWithTitle:MCLEStr(IDS_WINDOWS_EXIT)
                                                 content:MCLEStr(IDS_WARNING_ARCADE_TEXT)
                                                 buttons:@[MCLEStr(IDS_CANCEL), MCLEStr(IDS_OK)]
                                              focusIndex:0
                                                callback:^(MCLEDialogResult r) {
                                if (r != MCLEDialogResultDecline) return;
                                dispatch_after(
                                    dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)),
                                    dispatch_get_main_queue(), ^{
                                        NSLog(@"[MinecraftVC] Exit Game confirmed, terminating");
                                        mcle_audio_stop_menu_music();
                                        exit(0);
                                    });
                            }];
                        }
                    } else if ([self.currentMenuSwf isEqualToString:@"SaveMessage1080.swf"]) {
                        // UIScene_SaveMessage::handlePress
                        [self navigateToHomeMenu];
                    } else if ([self.currentMenuSwf isEqualToString:@"DeathMenu1080.swf"]) {
                        // UIScene_DeathMenu::handlePress: eAppAction_Respawn,
                        // ConnectingProgress while the player respawns
                        if (id == 0) {
                            [self openConnectingProgress:IDS_PROGRESS_RESPAWNING respawn:YES];
                            mcle_game_respawn();
                        } else {
                            [self requestExitGame];
                        }
                    } else if ([self.currentMenuSwf isEqualToString:@"PauseMenu1080.swf"]) {
                        // UIScene_PauseMenu::handlePress
                        switch (id) {
                            case 0: [self resumeFromPause]; break;
                            case 1: [self navigateForwardTo:@"HelpAndOptionsMenu1080.swf"]; break;
                            case 2: [self navigateForwardTo:@"LeaderboardMenu1080.swf"]; break;
                            case 4: {
                                // PerformActionSaveGame: the save is there, so
                                // ask before overwriting it. SaveGameDialogReturned
                                // saves on the second button.
                                __weak MinecraftViewController* weakSelf = self;
                                [self presentDialogWithTitle:MCLEStr(IDS_TITLE_SAVE_GAME)
                                                     content:MCLEStr(IDS_CONFIRM_SAVE_GAME)
                                                     buttons:@[MCLEStr(IDS_CONFIRM_CANCEL), MCLEStr(IDS_CONFIRM_OK)]
                                                  focusIndex:0
                                                    callback:^(MCLEDialogResult r) {
                                    if (r == MCLEDialogResultDecline) {
                                        MCLECaptureThumbnailThen(^{ [weakSelf openSaveProgress:NO]; });
                                    }
                                }];
                                break;
                            }
                            case 5: [self requestExitGame]; break;
                            // Achievements is the platform overlay
                            default:
                                NSLog(@"[pause] button %d not wired yet", id);
                                break;
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
                                // Title IDS_DEFAULTS_TITLE / body
                                // IDS_DEFAULTS_TEXT per
                                // strings_Misc.txt line 492-496.
                                // UIScene_SettingsMenu: Cancel / OK, and
                                // ResetDefaultsDialogReturned resets on OK
                                [self presentDialogWithTitle:MCLEStr(IDS_DEFAULTS_TITLE)
                                                     content:MCLEStr(IDS_DEFAULTS_TEXT)
                                                     buttons:@[MCLEStr(IDS_CONFIRM_CANCEL), MCLEStr(IDS_CONFIRM_OK)]
                                                  focusIndex:0
                                                    callback:^(MCLEDialogResult r) {
                                    if (r == MCLEDialogResultDecline) {
                                        mcle_settings_reset_to_defaults(mcle_world_is_ticking());
                                        // SetGameSettings applies each one,
                                        // the ones read every tick follow
                                        unsigned char music = mcle_settings_get(MCLE_SETTING_MusicVolume);
                                        unsigned char sfx = mcle_settings_get(MCLE_SETTING_SoundFXVolume);
                                        mcle_audio_set_music_volume(music);
                                        mcle_audio_set_sfx_volume(sfx);
                                        mcle_sound_update_music_volume(music / 100.0f);
                                        mcle_sound_update_sfx_volume(sfx / 100.0f);
                                        mcle_game_host_settings_changed();
                                    }
                                }];
                                break;
                        }
                        if (target) [self navigateForwardTo:target];
                    }
                }
            }
            // UIScene_Intro::handleInput: A skips the rest of the logos
            if (!g_pending_dialog && (pressedNow & 0x00000001u) &&
                [self.currentMenuSwf isEqualToString:@"Intro1080.swf"]) {
                [self introNavigate];
            }
            // B button (code=1, mask 0x00000002). Priority order:
            //   1. Dialog up -> Cancelled (regardless of which
            //      underlying scene is showing). Matches console
            //      UIScene_MessageBox::handleInput line 99-105.
            //   2. Otherwise on a non-MainMenu scene -> back.
            //   3. MainMenu B is a no-op (no scene to pop to).
            // UIScene_HowToPlay::handleInput: A next page, X previous
            if (!g_pending_dialog && [self.currentMenuSwf isEqualToString:@"HowToPlay1080.swf"]) {
                if ((pressedNow & 0x00000001u) && g_htp_page + 1 != mcle_htp_num_pages()) {
                    MCLEHowToPlayStartPage(g_htp_page + 1);
                    mcle_audio_play_ui_sfx("press", 1.0f, 1.0f);
                } else if ((pressedNow & 0x00000004u) && g_htp_page > 0) {
                    MCLEHowToPlayStartPage(g_htp_page - 1);
                    mcle_audio_play_ui_sfx("press", 1.0f, 1.0f);
                }
            }
            // UIScene_LaunchMoreOptionsMenu::handleInput: LB and RB swap the tab
            if (!g_pending_dialog && (pressedNow & (0x00000080u | 0x00000040u)) &&
                [self.currentMenuSwf isEqualToString:@"LaunchMoreOptionsMenu1080.swf"]) {
                MCLEMoreOptionsChangeTab();
            }
            // UIScene_LoadOrJoinMenu::handleInput: RB, and X on Windows64,
            // are the focused save's options
            if (!g_pending_dialog && (pressedNow & (0x00000040u | 0x00000004u)) &&
                [self.currentMenuSwf isEqualToString:@"LoadOrJoinMenu1080.swf"]) {
                [self loadOrJoinSaveOptions];
            }
            if (pressedNow & 0x00000002u) {
                if (g_pending_dialog) {
                    mcle_audio_play_ui_sfx("back", 1.0f, 1.0f);
                    [self finishDialogWithResult:MCLEDialogResultCancelled];
                } else if ([self.currentMenuSwf isEqualToString:@"DeathMenu1080.swf"]) {
                    // UIScene_DeathMenu::handleInput swallows B
                } else if ([self.currentMenuSwf isEqualToString:@"Intro1080.swf"] ||
                           [self.currentMenuSwf isEqualToString:@"SaveMessage1080.swf"]) {
                    // UIScene_Intro and UIScene_SaveMessage::handleInput only take A
                } else if ([self.currentMenuSwf isEqualToString:@"EndPoem1080.swf"]) {
                    // UIScene_EndPoem::handleInput: on to eAppAction_Respawn,
                    // ConnectingProgress while the player respawns
                    if (!g_endpoem_ignore_input) {
                        g_endpoem_ignore_input = true;
                        mcle_audio_play_ui_sfx("back", 1.0f, 1.0f);
                        // This just allows it to be shown
                        mcle_tutorial_show_popup(true);
                        g_world_won = false;
                        [self openConnectingProgress:IDS_PROGRESS_RESPAWNING respawn:YES];
                        mcle_game_respawn();
                    }
                } else if ([self.currentMenuSwf isEqualToString:@"PauseMenu1080.swf"]) {
                    // UIScene_PauseMenu::handleInput: B goes back to the game
                    mcle_audio_play_ui_sfx("back", 1.0f, 1.0f);
                    [self resumeFromPause];
                } else if (![self.currentMenuSwf isEqualToString:@"MainMenu1080.swf"] &&
                           ![self.currentMenuSwf isEqualToString:@"FullscreenProgress1080.swf"]) {
                    // FullscreenProgress has no cancel func for a save load
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
        // The scene after the intro has drawn, its white can go
        if (self.introBackdrop && ![self.currentMenuSwf isEqualToString:@"Intro1080.swf"]) {
            [self.introBackdrop removeFromSuperview];
            self.introBackdrop = nil;
        }
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
