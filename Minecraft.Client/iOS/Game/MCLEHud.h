#pragma once

#ifdef __cplusplus
#include <memory>
class ServerPlayer;
extern "C" {
#endif

// What IUIScene_HUD::updateFrameTick reads off the local player. Copied on
// the sim thread every tick, read by the HUD scene on the main thread.
typedef struct {
    int valid;
    int activeSlot;
    int canHurtPlayer;
    int regen;
    int health, lastHealth, blink, poison, wither;
    float absorption;
    int armour;
    int food, lastFood, hunger, saturation;
    int underWater, air, airExtra;
    int hasExperience;
    float expProgress;
    int expLevel, xpNeeded;
    // SetSelectedLabel / HideSelectedLabel, counted so the scene can tell
    // a new call happened
    int labelCalls, hideLabelCalls;
    char label[128];
    // CMinecraftApp's opacity timer, ticks left at full opacity
    int opacityTimer;
    // what the player rides: 0 nothing, 1 not a LivingEntity, 2 a LivingEntity
    int riding;
    int ridingJumpable;
    int riderHealth, riderMaxHealth;
    float jumpRidingScale;
    char displayName[64];
    // abilities.mayfly, what the controls picture calls creative
    int mayfly;
} MCLEHudState;

void mcle_hud_get_state(MCLEHudState *out);

// UIScene_HUD::tick's boss check on BossMobGuiInfo, which the boss's renderer
// sets as it draws: 1 with its health and name while it's shown, counting
// its display time down, 0 with no boss
int mcle_hud_boss(float *health, char *name, int cap);

// An Iggy custom draw region (an object named iggy_<name>), in pixels of
// the UI layer. Same layout as RuffleIosCustomDrawRegion.
typedef struct {
    char name[32];
    float x0, y0, x1, y1;
} MCLECustomDrawRegion;

// UIScene_HUD::customDraw for each region, drawn on the UI layer over the
// Ruffle HUD. Call once a frame in game, with no regions to clear it.
void mcle_hud_custom_draw(const MCLECustomDrawRegion *regions, int count);

#ifdef __cplusplus
}
#include <string>
void mcle_hud_update(const std::shared_ptr<ServerPlayer> &player);
void mcle_hud_set_selected_label(const std::wstring &label);
// CMinecraftApp::SetOpacityTimer, when the hotbar is scrolled
void mcle_hud_scrolled();
// Leaving the world: no state and no hotbar items left over
void mcle_hud_reset();
// Copies of the hotbar items from the last tick, for drawing them
class ItemInstance;
void mcle_hud_hotbar(std::shared_ptr<ItemInstance> out[9]);
#endif
