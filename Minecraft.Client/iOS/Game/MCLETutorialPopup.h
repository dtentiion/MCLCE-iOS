#pragma once

// UIComponent_TutorialPopup, the TutorialPopup movie the tutorial's messages
// show in. Its logic is in MCLETutorialPopup.cpp; what it does with Iggy
// comes back from here as calls for the view controller to make on the
// TutorialPopup1080.swf sibling, the way the container scenes' do.

#include <vector>

#include "MCLEContainerMenu.h"

// The game's side. UIKit has its own UIScene, so the view controller only
// gets the part below.
#ifndef __OBJC__
class Tutorial;
class UIScene;

// UIStructs.h's TutorialPopupInfo
struct MCLETutorialPopupInfo {
    UIScene *interactScene = nullptr;
    const wchar_t *desc = L"";
    const wchar_t *title = L"";
    int icon = -1;
    int iAuxVal = 0;
    bool isFoil = false;
    bool allowFade = true;
    bool isReminder = false;
    Tutorial *tutorial = nullptr;
};

// The UIController calls that reach the popup, from the tutorial on the sim
// thread or a menu on the main thread
void mcle_tutorial_popup_set_tutorial(Tutorial *tutorial);
void mcle_tutorial_popup_set_container_menu_visible(bool visible);
void mcle_tutorial_popup_set_description(MCLETutorialPopupInfo *info);
void mcle_tutorial_popup_remove_interact_scene(UIScene *scene);
void mcle_tutorial_popup_set_visible(bool visible);

// Leaving the world: UIController::CloseAllPlayersScenes empties the popup
// and the tutorial it was given goes with the game mode
void mcle_tutorial_popup_reset();
#endif

// UIController::IsTutorialVisible, the layer's flag for the popup
bool mcle_tutorial_popup_is_visible();

// The view controller's side, on the main thread.
// UIScene::initialiseMovie's part after the movie is up: the constructor's
// calls on it
void mcle_tutorial_popup_movie_ready();
// UIScene::tick, the fade and move timers
void mcle_tutorial_popup_tick();
// What the popup wants done on its movie since the last call. Paths are
// the popup's own, "" for its root.
std::vector<MCLEUIOp> mcle_tutorial_popup_take_ops();
// The popup's iggy_ regions this frame, then its customDraw for them,
// between mcle_custom_draw_begin and mcle_custom_draw_end
void mcle_tutorial_popup_set_regions(const MCLECustomDrawRegion *regions, int count);
bool mcle_tutorial_popup_has_icon();
void mcle_tutorial_popup_custom_draw();

// Tutorial::showTutorialPopup, for the scenes that hide it (the pause and
// death menus). Takes the sim tick lock.
void mcle_tutorial_show_popup(bool show);
