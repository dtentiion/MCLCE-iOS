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
} MCLEHudState;

void mcle_hud_get_state(MCLEHudState *out);

#ifdef __cplusplus
}
void mcle_hud_update(const std::shared_ptr<ServerPlayer> &player);
#endif
