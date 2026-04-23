// Game settings store. Mirrors console's GAME_SETTINGS / settings.dat
// (Common/Consoles_App.cpp SetGameSettings / GetGameSettings + the
// Win64_Save/LoadSettings pair at lines 784-817). Indices come from
// Common/App_enums.h enum eGameSetting.
//
// On iOS we keep a flat unsigned-char table keyed by that enum,
// persisted to Documents/settings.dat. That's simpler than porting
// the full bit-packed GAME_SETTINGS struct and equivalent in
// practice - console's GAME_SETTINGS is bit-packed to fit 204 bytes
// of profile data on Xbox, which doesn't apply here.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Matches eGameSetting in Common/App_enums.h. Only the values used
// by the five Settings scenes are enumerated; padding anything else
// zero on load keeps us forward-compatible.
enum mcle_setting {
    MCLE_SETTING_MusicVolume = 0,
    MCLE_SETTING_SoundFXVolume = 1,
    MCLE_SETTING_RenderDistance = 2,
    MCLE_SETTING_Gamma = 3,
    MCLE_SETTING_FOV = 4,
    MCLE_SETTING_Difficulty = 5,
    MCLE_SETTING_SensitivityInGame = 6,
    MCLE_SETTING_SensitivityInMenu = 7,
    MCLE_SETTING_ViewBob = 8,
    MCLE_SETTING_ControlScheme = 9,
    MCLE_SETTING_ControlInvertLook = 10,
    MCLE_SETTING_ControlSouthPaw = 11,
    MCLE_SETTING_SplitScreenVertical = 12,
    MCLE_SETTING_GamertagsVisible = 13,
    MCLE_SETTING_Autosave = 14,
    MCLE_SETTING_DisplaySplitscreenGamertags = 15,
    MCLE_SETTING_Hints = 16,
    MCLE_SETTING_InterfaceOpacity = 17,
    MCLE_SETTING_Tooltips = 18,
    MCLE_SETTING_Clouds = 19,
    MCLE_SETTING_Online = 20,
    MCLE_SETTING_InviteOnly = 21,
    MCLE_SETTING_FriendsOfFriends = 22,
    MCLE_SETTING_DisplayUpdateMessage = 23,
    MCLE_SETTING_BedrockFog = 24,
    MCLE_SETTING_DisplayHUD = 25,
    MCLE_SETTING_DisplayHand = 26,
    MCLE_SETTING_CustomSkinAnim = 27,
    MCLE_SETTING_DeathMessages = 28,
    MCLE_SETTING_UISize = 29,
    MCLE_SETTING_UISizeSplitscreen = 30,
    MCLE_SETTING_AnimatedCharacter = 31,
    MCLE_SETTING_MAX = 32,
};

// Call once at launch. Loads Documents/settings.dat if it exists,
// otherwise seeds the table with console's default profile (from
// CMinecraftApp::InitGameSettings in Common/Consoles_App.cpp).
void mcle_settings_load(void);

// Get / set individual values. set persists immediately to disk so
// we don't lose state if the sideloaded build crashes mid-session.
unsigned char mcle_settings_get(int setting);
void          mcle_settings_set(int setting, unsigned char value);

#ifdef __cplusplus
}
#endif
