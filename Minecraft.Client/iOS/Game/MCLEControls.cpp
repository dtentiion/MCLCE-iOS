// UIScene_ControlsMenu::PositionAllText and PositionText: which string goes
// on each of the controller picture's labels, from the control scheme's
// mappings. The scene side is in MinecraftViewController.mm.

// UIScene_ControlsMenu's e_Pad order, the same order the scene keeps its
// labels and lines in
enum {
    e_PadBack = 0,
    e_PadLT,
    e_PadLB,
    e_PadDPadLeft,
    e_PadDPadRight,
    e_PadDPadUp,
    e_PadDPadDown,
    e_PadLS_1,
    e_PadLS_2,
    e_PadStart,
    e_PadRT,
    e_PadRB,
    e_PadY,
    e_PadB,
    e_PadA,
    e_PadX,
    e_PadRS_1,
    e_PadRS_2,
    e_PadTouch,
    e_PadCOUNT,
};

namespace {

struct ControlsText {
    int layout;
    int text[e_PadCOUNT];

    // PositionTextDirect
    void direct(int iTextID, int index) { text[index] = iTextID; }

    void position(int iTextID, unsigned char ucAction) {
        unsigned int uiVal = InputManager.GetGameJoypadMaps(layout, ucAction);

        if (uiVal & _360_JOY_BUTTON_A) direct(iTextID, e_PadA);
        if (uiVal & _360_JOY_BUTTON_B) direct(iTextID, e_PadB);
        if (uiVal & _360_JOY_BUTTON_X) direct(iTextID, e_PadX);
        if (uiVal & _360_JOY_BUTTON_Y) direct(iTextID, e_PadY);
        if (uiVal & _360_JOY_BUTTON_BACK) direct(iTextID, e_PadBack);
        if (uiVal & _360_JOY_BUTTON_START) direct(iTextID, e_PadStart);
        if (uiVal & _360_JOY_BUTTON_RB) direct(iTextID, e_PadRB);
        if (uiVal & _360_JOY_BUTTON_LB) direct(iTextID, e_PadLB);
        if (uiVal & _360_JOY_BUTTON_RTHUMB) direct(iTextID, e_PadRS_1);
        if (uiVal & _360_JOY_BUTTON_LTHUMB) direct(iTextID, e_PadLS_1);
        // Look
        if (uiVal & _360_JOY_BUTTON_RSTICK_RIGHT) direct(iTextID, e_PadRS_2);
        // Move
        if (uiVal & _360_JOY_BUTTON_LSTICK_RIGHT) direct(iTextID, e_PadLS_2);
        if (uiVal & _360_JOY_BUTTON_RT) direct(iTextID, e_PadRT);
        if (uiVal & _360_JOY_BUTTON_LT) direct(iTextID, e_PadLT);
        if (uiVal & _360_JOY_BUTTON_DPAD_RIGHT) direct(iTextID, e_PadDPadRight);
        if (uiVal & _360_JOY_BUTTON_DPAD_LEFT) direct(iTextID, e_PadDPadLeft);
        if (uiVal & _360_JOY_BUTTON_DPAD_UP) direct(iTextID, e_PadDPadUp);
        if (uiVal & _360_JOY_BUTTON_DPAD_DOWN) direct(iTextID, e_PadDPadDown);
    }
};

} // namespace

// Each label's string id for the layout being looked at, -1 where the
// label is blank and its line hidden
extern "C" void mcle_controls_text(int layout, int creativeMode, int southPaw, int *out) {
    ControlsText t;
    t.layout = layout;
    for (int i = 0; i < e_PadCOUNT; ++i) t.text[i] = -1;

    if (creativeMode) {
        t.position(IDS_CONTROLS_JUMPFLY, MINECRAFT_ACTION_JUMP);
    } else {
        t.position(IDS_CONTROLS_JUMP, MINECRAFT_ACTION_JUMP);
    }
    t.position(IDS_CONTROLS_INVENTORY, MINECRAFT_ACTION_INVENTORY);
    t.position(IDS_CONTROLS_PAUSE, MINECRAFT_ACTION_PAUSEMENU);
    if (creativeMode) {
        t.position(IDS_CONTROLS_SNEAKFLY, MINECRAFT_ACTION_SNEAK_TOGGLE);
    } else {
        t.position(IDS_CONTROLS_SNEAK, MINECRAFT_ACTION_SNEAK_TOGGLE);
    }
    t.position(IDS_CONTROLS_USE, MINECRAFT_ACTION_USE);
    t.position(IDS_CONTROLS_ACTION, MINECRAFT_ACTION_ACTION);
    t.position(IDS_CONTROLS_HELDITEM, MINECRAFT_ACTION_RIGHT_SCROLL);
    t.position(IDS_CONTROLS_HELDITEM, MINECRAFT_ACTION_LEFT_SCROLL);
    t.position(IDS_CONTROLS_DROP, MINECRAFT_ACTION_DROP);
    t.position(IDS_CONTROLS_CRAFTING, MINECRAFT_ACTION_CRAFTING);
    t.position(IDS_CONTROLS_THIRDPERSON, MINECRAFT_ACTION_RENDER_THIRD_PERSON);
    t.position(IDS_CONTROLS_PLAYERS, MINECRAFT_ACTION_GAME_INFO);

    // Swap for southpaw
    if (southPaw) {
        t.position(IDS_CONTROLS_LOOK, MINECRAFT_ACTION_RIGHT);
        t.position(IDS_CONTROLS_MOVE, MINECRAFT_ACTION_LOOK_RIGHT);
    } else {
        t.position(IDS_CONTROLS_MOVE, MINECRAFT_ACTION_RIGHT);
        t.position(IDS_CONTROLS_LOOK, MINECRAFT_ACTION_LOOK_RIGHT);
    }

    // Layout 0 in creative shows the d-pad for flying
    bool layoutHasDpadFly = layout == 0;
    if (creativeMode && layoutHasDpadFly) {
        t.position(IDS_CONTROLS_DPAD, MINECRAFT_ACTION_DPAD_LEFT);
    }

    for (int i = 0; i < e_PadCOUNT; ++i) out[i] = t.text[i];
}
