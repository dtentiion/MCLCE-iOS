#pragma once

// The container menus (UIScene_InventoryMenu and UIScene_CraftingMenu for
// now). The scene logic is upstream's IUIScene_ classes, what the
// UIScene_ classes do with Iggy comes back from here as calls for the view
// controller to make on the movie. Everything here runs on the main thread
// unless it says otherwise; whatever touches the player holds the sim tick
// lock.

#include <memory>
#include <string>
#include <vector>

#include "MCLEHud.h"

class ItemInstance;

// One IggyPlayerCallMethodRS, a UI sound to play, or a key for the movie
struct MCLEUIArg {
    enum Kind { Number, Bool, String } kind;
    double number;
    std::string str;
};

struct MCLEUIOp {
    enum Kind { Call, Sound, Key } kind;
    std::string path;   // value path, "" for the root
    std::string method; // the method, or the sound's name
    // the call's arguments; a sound's pitch; a key's Iggy key code and
    // whether it went down
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

// CreativeMenu1080's MainPanel children the scene maps
struct MCLECreativeLayout {
    MCLEControlRect backgroundPanel;
    MCLEControlRect containerList;
    MCLEControlRect hotbarList;
    MCLEControlRect tabs[8]; // TouchPanel_0 to TouchPanel_7
    MCLEControlRect slider;  // TouchPanel_Slider
};

// UIScene_CreativeMenu's constructor
void mcle_container_open_creative(const MCLECreativeLayout &layout);

// The menu the world asked for, taken once a frame while none is up
enum MCLEContainerRequest {
    MCLE_CONTAINER_NONE,
    MCLE_CONTAINER_CRAFTING_2X2, // app.LoadCrafting2x2Menu
    MCLE_CONTAINER_CRAFTING_3X3, // the server opened a crafting table
    MCLE_CONTAINER_CREATIVE,     // app.LoadCreativeMenu
    MCLE_CONTAINER_SERVER,       // another container the server opened
};
int mcle_container_take_request();

// From the sim thread: Minecraft::tick's crafting action (the 2x2, or the
// creative menu in creative), and ClientConnection::handleContainerOpen for
// the ContainerOpenPacket the local player's ServerPlayer sends
void mcle_container_request(int request);
void mcle_container_open_packet(int type, int containerId, const std::wstring &title, bool customName, int size);

// How the scenes for the server's containers read their controls' x, y,
// width and height off the movie
void mcle_container_set_rect_reader(MCLEControlRect (*reader)(const char *path));

// The movie for the container an MCLE_CONTAINER_SERVER request is for, and
// its scene once the movie is up (false when it couldn't open)
const char *mcle_container_server_movie();
bool mcle_container_open_server();

// UIScene_CraftingMenu's constructor, for the 2x2 or the table's 3x3.
// False when the 3x3 has no crafting menu to show.
bool mcle_container_open_crafting(bool table);

// handleContainerOpen when the menu couldn't open: the client's
// ContainerClosePacket, so the server closes what it opened
void mcle_container_open_failed();

// One UI frame: the pad's presses (UIController::handleInput) then the
// scene's tick. `prevButtons` is the last frame's.
void mcle_container_frame(unsigned int buttons, unsigned int prevButtons, double now);

// What the scene wants done on the movie since the last call
std::vector<MCLEUIOp> mcle_container_take_ops();

// The tooltip strip, A B X Y LT RT LB RB LS RS Back, -1 for hidden
void mcle_container_tooltips(int out[11]);

// The stage point IggyMakeEventMouseMove goes to, false without a pointer
bool mcle_container_pointer(float *x, float *y);

// ui.CloseUIScenes from the scene
bool mcle_container_close_requested();

// The scene's handleDestroy
void mcle_container_close();

bool mcle_container_is_open();

// The scene's customDraw for the movie's iggy_ regions
void mcle_container_custom_draw(const MCLECustomDrawRegion *regions, int count);

// MCLECustomDraw.cpp, the UI layer the HUD draws its items on
bool mcle_custom_draw_begin();
void mcle_custom_draw_slot(const MCLECustomDrawRegion &region, std::shared_ptr<ItemInstance> item, float alpha,
                           bool decorations);
void mcle_custom_draw_end();
