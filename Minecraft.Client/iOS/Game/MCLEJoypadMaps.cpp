// InputManager's controller mappings: which buttons each action uses in
// each of the three control schemes. The table is Windows64's own,
// DefineActions copied out of Windows64_Minecraft.cpp by
// scripts/gen-joypad-maps.py and run once against this table.

#include <mutex>

extern "C" unsigned char mcle_settings_get(int setting); // MCLE_iOS_Settings.mm

namespace {

const int kSettingControlScheme = 9; // MCLE_SETTING_ControlScheme

// C_4JInput's map, for DefineActions to fill
struct JoypadMaps {
    unsigned int maps[3][256] = {};
    void SetGameJoypadMaps(unsigned char style, unsigned char action, unsigned int buttons) {
        if (style < 3) maps[style][action] = buttons;
    }
};

JoypadMaps g_maps;
std::once_flag g_defined;

void defineActions() {
    // DefineActions talks to InputManager
    JoypadMaps &InputManager = g_maps;
#include "../../../upstream/Minecraft.Client/iOS_DefineActions.inc"
}

} // namespace

// C_4JInput::GetGameJoypadMaps
extern "C" unsigned int mcle_joypad_map(int style, int action) {
    std::call_once(g_defined, defineActions);
    if (style < 0 || style > 2 || action < 0 || action > 255) return 0;
    return g_maps.maps[style][action];
}

// C_4JInput::GetJoypadMapVal: the pad's control scheme, which
// app.SetGameSettings(eGameSetting_ControlScheme) hands to InputManager
extern "C" int mcle_joypad_map_val(int pad) {
    (void)pad;
    int scheme = mcle_settings_get(kSettingControlScheme);
    return (scheme >= 0 && scheme <= 2) ? scheme : 0;
}
