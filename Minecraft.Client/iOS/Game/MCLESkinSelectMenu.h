#pragma once

// Change Skin, UIScene_SkinSelectMenu. What it does to the movie is queued
// with the container menus' UI calls (mcle_container_take_ops) and its
// tooltips are theirs too (mcle_container_tooltips). Main thread only.

#include "MCLEHud.h"

// The constructor, once SkinSelectMenu1080 is up
void mcle_skin_select_open(double now);
bool mcle_skin_select_is_open();

// One UI frame: the pad's presses (UIController::handleInput) then the
// scene's tick. `prevButtons` is the last frame's. The scene's tick runs
// the movie's frames through `tickMovie`, one per call, like
// UIScene::tick's IggyPlayerTickRS.
void mcle_skin_select_frame(unsigned int buttons, unsigned int prevButtons, double now, void (*tickMovie)());

// navigateBack from the scene
bool mcle_skin_select_close_requested();
void mcle_skin_select_close();

// The movie's handleAnimationEnd callback
void mcle_skin_select_animation_end();

// The scene's customDraw for the movie's iggy_Character regions
void mcle_skin_select_custom_draw(const MCLECustomDrawRegion *regions, int count);
