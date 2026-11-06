#pragma once

// The container menus (UIScene_InventoryMenu for now). The scene logic is
// upstream's IUIScene_AbstractContainerMenu and IUIScene_InventoryMenu,
// what UIScene_AbstractContainerMenu does with Iggy comes back from here as
// calls for the view controller to make on the movie. Everything here runs
// on the main thread; whatever touches the player holds the sim tick lock.

#include <memory>
#include <string>
#include <vector>

#include "MCLEHud.h"

class ItemInstance;

// One IggyPlayerCallMethodRS, or a UI sound to play
struct MCLEUIArg {
    enum Kind { Number, Bool, String } kind;
    double number;
    std::string str;
};

struct MCLEUIOp {
    enum Kind { Call, Sound } kind;
    std::string path;   // value path, "" for the root
    std::string method; // the method, or the sound's name
    std::vector<MCLEUIArg> args;
};

// A control's x, y, width and height as UIControl reads them, x and y in
// MainPanel's coordinates
struct MCLEControlRect {
    float x, y, w, h;
};

// InventoryMenu1080's MainPanel children the scene maps
struct MCLEInventoryLayout {
    MCLEControlRect backgroundPanel;
    MCLEControlRect hotbarList;
    MCLEControlRect inventoryList;
    MCLEControlRect armorList;
    MCLEControlRect player; // iggy_player, UIControl_MinecraftPlayer
};

// Minecraft::tick's inventory button: the buttons it's on in the pad's
// scheme, and whether it would open the inventory menu now
unsigned int mcle_container_inventory_buttons();
bool mcle_container_can_open_inventory();

// UIScene_InventoryMenu's constructor
void mcle_container_open_inventory(const MCLEInventoryLayout &layout, double now);

// One UI frame: the pad's presses (UIScene::handleInput) then
// UIScene_AbstractContainerMenu::tick. `prevButtons` is the last frame's.
void mcle_container_frame(unsigned int buttons, unsigned int prevButtons, double now);

// What the scene wants done on the movie since the last call
std::vector<MCLEUIOp> mcle_container_take_ops();

// The tooltip strip, A B X Y LT RT LB RB LS RS Back, -1 for hidden
void mcle_container_tooltips(int out[11]);

// m_pointerPos, the stage point IggyMakeEventMouseMove goes to
void mcle_container_pointer(float *x, float *y);

// ui.CloseUIScenes from the scene
bool mcle_container_close_requested();

// UIScene_AbstractContainerMenu::handleDestroy
void mcle_container_close();

bool mcle_container_is_open();

// UIScene_AbstractContainerMenu::customDraw for the movie's iggy_ regions,
// drawn from what the last frame saw in the slots
void mcle_container_custom_draw(const MCLECustomDrawRegion *regions, int count);

// MCLECustomDraw.cpp, the UI layer the HUD draws its items on
bool mcle_custom_draw_begin();
void mcle_custom_draw_slot(const MCLECustomDrawRegion &region, std::shared_ptr<ItemInstance> item, float alpha,
                           bool decorations);
void mcle_custom_draw_end();
