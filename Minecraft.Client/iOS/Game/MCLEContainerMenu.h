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

// One IggyPlayerCallMethodRS, a UI sound to play, a key for the movie, or a
// control's visible flag
struct MCLEUIArg {
    enum Kind { Number, Bool, String } kind;
    double number;
    std::string str;
};

struct MCLEUIOp {
    enum Kind { Call, Sound, Key, Visible } kind;
    std::string path;   // value path, "" for the root
    std::string method; // the method, or the sound's name
    // the call's arguments; a sound's pitch; a key's Iggy key code and
    // whether it went down; whether the control is visible
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
// scheme, and whether it would open the inventory menu now. Riding a horse
// it asks the server for the horse's inventory instead.
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
void mcle_container_open_packet(int type, int containerId, const std::wstring &title, bool customName, int size,
                                int entityId);

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

// How UIControl_Base::getLabel reads a control's label off the movie,
// false when it couldn't
void mcle_container_set_label_reader(bool (*reader)(const char *path, std::wstring *label));

// The movie's handlePress, and handleFocusChange / handleInitFocus, for the
// scene that's up
void mcle_container_press(int controlId, int childId);
void mcle_container_focus_change(int controlId, int childId, bool init);
// and its handleCheckboxToggled
void mcle_container_checkbox_toggled(int controlId, bool selected);

// Whether the scene that's up has the menu background under it
bool mcle_container_menu_background();

// A scene the one that's up navigated to, taken once: its movie, null for
// none. Then its scene, made by the init for that movie, and done once the
// transition is over, which gives up on it if the movie never came up.
const char *mcle_container_take_navigate();
void mcle_container_open_navigated();
void mcle_container_navigate_done();

// When the close the scene asked for is a navigateBack to a scene under
// it: that scene's movie, otherwise null and the whole menu closes
const char *mcle_container_back_movie();
// Closes the scene that's up for the one under it, and reloadMovie on that
// one once its movie is up again
void mcle_container_navigate_back();
void mcle_container_reload();

// The keyboard a scene asked for, taken once; then its answer, ok false
// when it was cancelled
bool mcle_container_take_keyboard(std::wstring *title, std::wstring *text, int *maxChars);
void mcle_container_keyboard_done(bool ok, const std::wstring &text);

// The scenes' handleDestroy, the one that's up and any under it
void mcle_container_close();

bool mcle_container_is_open();

// The scene's customDraw for the movie's iggy_ regions
void mcle_container_custom_draw(const MCLECustomDrawRegion *regions, int count);

// MCLECustomDraw.cpp, the UI layer the HUD draws its items on
bool mcle_custom_draw_begin();
void mcle_custom_draw_slot(const MCLECustomDrawRegion &region, std::shared_ptr<ItemInstance> item, float alpha,
                           bool decorations);
void mcle_custom_draw_end();
// UIController::setupCustomDrawGameState, for a region that isn't a slot
void mcle_custom_draw_setup_state();
