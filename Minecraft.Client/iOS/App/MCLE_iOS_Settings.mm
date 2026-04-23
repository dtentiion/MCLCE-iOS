// Flat settings store keyed by mcle_setting. Persists to
// Documents/settings.dat on every set so we match console's
// Win64_SaveSettings write-on-change pattern
// (Common/Consoles_App.cpp:792).

#import <Foundation/Foundation.h>

#include "MCLE_iOS_Settings.h"

#include <atomic>
#include <mutex>
#include <cstdio>

namespace {

std::mutex g_mu;
unsigned char g_values[MCLE_SETTING_MAX] = {};
std::atomic<bool> g_loaded{false};

NSString* settings_path() {
    NSString* docs = [NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    return [docs stringByAppendingPathComponent:@"settings.dat"];
}

// Console defaults from CMinecraftApp::InitGameSettings
// (Common/Consoles_App.cpp:820+). Any setting not explicitly
// assigned stays at 0, matching console's memset-then-set pattern.
void apply_defaults() {
    // Audio (DEFAULT_VOLUME_LEVEL is 100 on console).
    g_values[MCLE_SETTING_MusicVolume] = 100;
    g_values[MCLE_SETTING_SoundFXVolume] = 100;

    // Graphics
    g_values[MCLE_SETTING_RenderDistance] = 16;         // "Far"
    g_values[MCLE_SETTING_Gamma] = 50;
    g_values[MCLE_SETTING_FOV] = 0;                     // slider val 0 -> 70 deg
    g_values[MCLE_SETTING_Clouds] = 1;
    g_values[MCLE_SETTING_BedrockFog] = 0;
    g_values[MCLE_SETTING_CustomSkinAnim] = 1;
    g_values[MCLE_SETTING_InterfaceOpacity] = 80;

    // Options
    g_values[MCLE_SETTING_Difficulty] = 1;              // easy (iOS matches console default)
    g_values[MCLE_SETTING_ViewBob] = 1;
    g_values[MCLE_SETTING_Hints] = 1;
    g_values[MCLE_SETTING_Tooltips] = 1;
    g_values[MCLE_SETTING_Autosave] = 2;                // 30 minutes

    // Control
    g_values[MCLE_SETTING_ControlScheme] = 0;
    g_values[MCLE_SETTING_ControlInvertLook] = 0;
    g_values[MCLE_SETTING_ControlSouthPaw] = 0;
    g_values[MCLE_SETTING_SensitivityInGame] = 100;
    g_values[MCLE_SETTING_SensitivityInMenu] = 100;

    // UI
    g_values[MCLE_SETTING_DisplayHUD] = 1;
    g_values[MCLE_SETTING_DisplayHand] = 1;
    g_values[MCLE_SETTING_DisplaySplitscreenGamertags] = 1;
    g_values[MCLE_SETTING_GamertagsVisible] = 1;
    g_values[MCLE_SETTING_SplitScreenVertical] = 0;
    g_values[MCLE_SETTING_DeathMessages] = 1;
    g_values[MCLE_SETTING_AnimatedCharacter] = 1;
    g_values[MCLE_SETTING_UISize] = 1;
    g_values[MCLE_SETTING_UISizeSplitscreen] = 2;

    // Network
    g_values[MCLE_SETTING_Online] = 1;
    g_values[MCLE_SETTING_InviteOnly] = 0;
    g_values[MCLE_SETTING_FriendsOfFriends] = 1;
}

void save_locked() {
    NSString* path = settings_path();
    FILE* f = std::fopen(path.UTF8String, "wb");
    if (!f) {
        NSLog(@"[settings] write open failed: %@", path);
        return;
    }
    std::fwrite(g_values, 1, MCLE_SETTING_MAX, f);
    std::fclose(f);
}

} // anon

extern "C" void mcle_settings_load(void) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_loaded.load()) return;
    apply_defaults();

    NSString* path = settings_path();
    if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
        FILE* f = std::fopen(path.UTF8String, "rb");
        if (f) {
            unsigned char buf[MCLE_SETTING_MAX] = {};
            size_t n = std::fread(buf, 1, MCLE_SETTING_MAX, f);
            std::fclose(f);
            // Tolerate shorter files: newer builds may have more
            // settings than an old settings.dat was written with.
            // Copy what we got, leave defaults for the rest.
            if (n > 0) {
                if (n > MCLE_SETTING_MAX) n = MCLE_SETTING_MAX;
                std::memcpy(g_values, buf, n);
                NSLog(@"[settings] loaded %zu byte(s) from %@", n, path);
            }
        }
    } else {
        NSLog(@"[settings] no settings.dat, using console defaults");
        save_locked();
    }
    g_loaded.store(true);
}

extern "C" unsigned char mcle_settings_get(int setting) {
    if (setting < 0 || setting >= MCLE_SETTING_MAX) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    return g_values[setting];
}

extern "C" void mcle_settings_set(int setting, unsigned char value) {
    if (setting < 0 || setting >= MCLE_SETTING_MAX) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_values[setting] == value) return;
    g_values[setting] = value;
    save_locked();
}
