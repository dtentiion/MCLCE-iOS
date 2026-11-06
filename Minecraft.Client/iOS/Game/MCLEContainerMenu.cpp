// The container menus' frame loop and what they share, and the inventory.
// IUIScene_AbstractContainerMenu, the pointer, slot and tooltip logic the
// slot menus share, is upstream's own: its body is copied out by
// scripts/gen-container-menu.py and compiled at the bottom of the includes
// here. IUIScene_InventoryMenu is built with the rest of the world. The two
// classes below stand in for UIScene_AbstractContainerMenu and
// UIScene_InventoryMenu, and what those do with Iggy is queued as
// MCLEUIOps for the view controller to make on the movie.

#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"
#include "MCLEAbstractContainerScene.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>

#include "../../../upstream/Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.crafting.h"
#include "../../../upstream/Minecraft.World/FurnaceTileEntity.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.effect.h"
#include "../../../upstream/Minecraft.World/HtmlString.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/SoundTypes.h"
#include "../../../upstream/Minecraft.World/ContainerOpenPacket.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/Options.h"
#include "../../../upstream/Minecraft.Client/EntityRenderDispatcher.h"
#include "../../../upstream/Minecraft.Client/Lighting.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/TexturePack.h"
#include "../../../upstream/Minecraft.Client/TexturePackRepository.h"
#include "../../../upstream/Minecraft.Client/Common/Colours/ColourTable.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_AbstractContainerMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_InventoryMenu.h"

void mcle_game_set_menu_displayed(bool displayed); // MCLEGameLoop.cpp

namespace {

std::vector<MCLEUIOp> g_ops;

// UIComponent_Tooltips as UIController::SetTooltips and friends leave it
int g_tooltipText[eToolTipNumButtons];
bool g_tooltipShown[eToolTipNumButtons];

bool g_closeRequested = false;

MCLEContainerScene *g_scene = nullptr;

std::string toUtf8(const std::wstring &w) {
    std::string s;
    for (wchar_t wc : w) {
        unsigned int c = static_cast<unsigned int>(wc);
        if (c < 0x80) {
            s += static_cast<char>(c);
        } else if (c < 0x800) {
            s += static_cast<char>(0xC0 | (c >> 6));
            s += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            s += static_cast<char>(0xE0 | ((c >> 12) & 0x0F));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return s;
}

// UIComponent_Tooltips::_SetTooltip: -2 shows the button with no text
void setTooltip(int button, int ids) {
    if (button < 0 || button >= eToolTipNumButtons) return;
    g_tooltipText[button] = ids;
    g_tooltipShown[button] = ids > -1 || ids == -2;
}

void resetTooltips() {
    for (int i = 0; i < eToolTipNumButtons; ++i) {
        g_tooltipText[i] = -1;
        g_tooltipShown[i] = false;
    }
}

} // namespace

MCLEUIArg mcle_ui_num(double v) { return MCLEUIArg{MCLEUIArg::Number, v, std::string()}; }
MCLEUIArg mcle_ui_bool(bool v) { return MCLEUIArg{MCLEUIArg::Bool, v ? 1.0 : 0.0, std::string()}; }
MCLEUIArg mcle_ui_str(const std::wstring &v) { return MCLEUIArg{MCLEUIArg::String, 0.0, toUtf8(v)}; }

void mcle_ui_call(const std::string &path, const char *method, std::vector<MCLEUIArg> args) {
    g_ops.push_back(MCLEUIOp{MCLEUIOp::Call, path, method, std::move(args)});
}

void mcle_ui_key(int iggyKeyCode, bool down) {
    g_ops.push_back(MCLEUIOp{MCLEUIOp::Key, std::string(), std::string(), {mcle_ui_num(iggyKeyCode), mcle_ui_bool(down)}});
}

MCLEContainerUI mcleContainerUI;

// UIController::PlayUISFX
void MCLEContainerUI::PlayUISFX(int sfx) {
    // ConsoleSoundEngine::wchUISoundNames
    static const char *const kNames[] = {"back", "craft", "craftfail", "focus", "press", "scroll"};
    static std::mt19937 s_random{std::random_device{}()};
    static std::uniform_real_distribution<float> s_distribution(0.0f, 1.0f);
    static long long s_lastUiSfx = 0;
    if (sfx < 0 || sfx >= eSFX_MAX) return;

    // Don't play multiple SFX on the same tick
    long long time = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count();
    if (time - s_lastUiSfx < 10) return;
    s_lastUiSfx = time;

    float pitch = 1.0f;
    if (sfx == eSFX_Focus) pitch += (s_distribution(s_random) - 0.5f) / 10;
    g_ops.push_back(MCLEUIOp{MCLEUIOp::Sound, std::string(), kNames[sfx], {mcle_ui_num(pitch)}});
}

void MCLEContainerUI::ShowTooltip(int, int button, bool show) {
    if (button >= 0 && button < eToolTipNumButtons) g_tooltipShown[button] = show;
}

void MCLEContainerUI::SetTooltipText(int, int button, int ids) { setTooltip(button, ids); }

void MCLEContainerUI::SetTooltips(int, int iA, int iB, int iX, int iY, int iLT, int iRT, int iLB, int iRB, int iLS,
                                  int iRS, int iBack, bool) {
    const int ids[] = {iA, iB, iX, iY, iLT, iRT, iLB, iRB, iLS, iRS, iBack};
    for (int i = 0; i < 11; ++i) setTooltip(i, ids[i]);
}

void MCLEContainerUI::NavigateBack(int) { g_closeRequested = true; }
void MCLEContainerUI::CloseUIScenes(int) { g_closeRequested = true; }

bool MCLEContainerScene::allowRepeat(int key) {
    switch (key) {
    case ACTION_MENU_OK:
    case ACTION_MENU_CANCEL:
    case ACTION_MENU_A:
    case ACTION_MENU_B:
    case ACTION_MENU_X:
    case ACTION_MENU_Y:
        return false;
    }
    return true;
}

void MCLEUILabel::init(const std::wstring &text) {
    label = text;
    mcle_ui_call(path, "Init", {mcle_ui_str(text)});
}

// UIControl_Base::setLabel
void MCLEUILabel::setLabel(const std::wstring &text, bool instant, bool force) {
    if (force || ((!label.empty() || !text.empty()) && label.compare(text) != 0)) changed = true;
    label = text;
    if (changed && instant) {
        changed = false;
        mcle_ui_call(path, "SetLabel", {mcle_ui_str(label)});
    }
}

// UIControl_Base::tick
void MCLEUILabel::tick() {
    if (!changed) return;
    changed = false;
    mcle_ui_call(path, "SetLabel", {mcle_ui_str(label)});
}

void MCLEUIProgress::init(const std::wstring &label, int id, int minValue, int maxValue, int current) {
    min = minValue;
    max = maxValue;
    mcle_ui_call(path, "Init", {mcle_ui_str(label)});
}

void MCLEUIProgress::setProgress(int current) {
    float percent = static_cast<float>(current - min) / (max - min);
    if (percent != lastPercent) {
        lastPercent = percent;
        mcle_ui_call(path, "setProgress", {mcle_ui_num(percent)});
    }
}

void MCLEUISlotList::addSlot(int id) { mcle_ui_call(path, "addSlot", {mcle_ui_num(id), mcle_ui_bool(false), mcle_ui_bool(false)}); }

void MCLEUISlotList::addSlots(int start, int count) {
    for (int i = start; i < start + count; ++i) addSlot(i);
}

void MCLEUISlotList::setHighlightSlot(int index) {
    if (index != lastHighlighted) {
        if (lastHighlighted != -1) setSlotHighlighted(lastHighlighted, false);
        setSlotHighlighted(index, true);
        lastHighlighted = index;
    }
}

void MCLEUISlotList::setSlotHighlighted(int index, bool highlight) {
    mcle_ui_call(path, "SetSlotHighlight", {mcle_ui_num(index), mcle_ui_bool(highlight)});
}

void MCLEUISlotList::showSlotRedBox(int index, bool show) {
    mcle_ui_call(path, "SetSlotRedBox", {mcle_ui_num(index), mcle_ui_bool(show)});
}

void MCLEUISlotList::setFocus(bool focus) {
    if (lastHighlighted != -1) setSlotHighlighted(lastHighlighted, focus);
}

void mcle_container_prepare() {
    g_ops.clear();
    resetTooltips();
    g_closeRequested = false;
}

void mcle_container_begin(MCLEContainerScene *scene) {
    g_scene = scene;
    mcle_game_set_menu_displayed(true);
    g_scene->snapshot();
}

namespace {

// Windows64's keyboard and mouse, which there isn't one of here
struct NoKeyboardMouse {
    bool IsKeyDown(int) { return false; }
    bool IsMouseGrabbed() { return false; }
    bool IsKBMActive() { return false; }
    bool WasMouseWheelConsumed() { return false; }
    int GetMouseDeltaX() { return 0; }
    int GetMouseDeltaY() { return 0; }
};
NoKeyboardMouse mcleNoKeyboardMouse;

// The pointer snap point onMouseTick keeps
struct SnapPoint {
    float x = 0, y = 0, z = 0;
};

} // namespace

// The local player is the server player: the menu goes on it and the
// carried item comes off it
static std::shared_ptr<Player> mcle_container_player() {
    return mcle_local_player();
}

// MultiPlayerGameMode::handleInventoryMouseClick would click the client's
// copy of the container and send the click to the server, where
// PlayerConnection::handleContainerClick clicks it again. The two are the
// same container here, so it's clicked once, the way the server does it.
// The ContainerAckPacket it sends is left out, there's no connection, and
// ignoreSlotUpdateHack keeps slotChanged and broadcastCarriedItem from
// sending too.
static void mcle_container_click(AbstractContainerMenu *menu, int slotId, int buttonNum, bool quickKey) {
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player || !menu || player->containerMenu != menu) return;
    player->resetLastActionTime();
    menu->clicked(slotId, buttonNum,
                  quickKey ? AbstractContainerMenu::CLICK_QUICK_MOVE : AbstractContainerMenu::CLICK_PICKUP, player);
    player->ignoreSlotUpdateHack = true;
    menu->broadcastChanges();
    player->broadcastCarriedItem();
    player->ignoreSlotUpdateHack = false;
}

#ifndef VK_LSHIFT
#define VK_LSHIFT 0xA0
#endif

#define ui mcleContainerUI
#define g_KBMInput mcleNoKeyboardMouse
#define D3DXVECTOR3 SnapPoint
#include "../../../upstream/Minecraft.Client/iOS_AbstractContainerMenu.inc"
#undef D3DXVECTOR3
#undef g_KBMInput
#undef ui

namespace {

// UIScene_InventoryMenu
class InventoryScene : public MCLEAbstractContainerScene, public IUIScene_InventoryMenu {
public:
    InventoryScene(const MCLEInventoryLayout &layout, const std::shared_ptr<ServerPlayer> &player, double now)
        : m_slotListArmor("MainPanel.armorList"), m_playerRect(layout.player) {
        m_backgroundPanel = layout.backgroundPanel;
        m_slotListHotbar.rect = layout.hotbarList;
        m_slotListInventory.rect = layout.inventoryList;
        m_slotListArmor.rect = layout.armorList;

        InventoryMenu *menu = static_cast<InventoryMenu *>(player->inventoryMenu);

        // Upstream awards GenericStats::openInventory() here through
        // LocalPlayer::awardStat. It's a locally awarded achievement, so the
        // ServerPlayer would just drop it, and nothing gives achievements
        // here yet, so it's left out.

        Initialize(0, menu, false, InventoryMenu::INV_SLOT_START, eSectionInventoryUsing, eSectionInventoryMax, false);

        m_slotListArmor.addSlots(InventoryMenu::ARMOR_SLOT_START,
                                 InventoryMenu::ARMOR_SLOT_END - InventoryMenu::ARMOR_SLOT_START);

        for (unsigned int i = 0; i < MobEffect::NUM_EFFECTS; ++i) m_bEffectTime[i] = 0;

        updateEffectsDisplay(player);
        // addTimer(INVENTORY_UPDATE_EFFECTS_TIMER_ID, 1000)
        m_effectsTimer = now + 1.0;
    }

    // UIScene_InventoryMenu::customDraw, the player, then the slots
    void customDraw(const MCLECustomDrawRegion &region) override {
        if (strcmp(region.name, "player") == 0) {
            drawPlayerPreview(region);
            return;
        }
        MCLEAbstractContainerScene::customDraw(region);
    }

protected:
    MCLEUISlotList m_slotListArmor;
    // The iggy_player control, MainPanel coordinates
    MCLEControlRect m_playerRect;
    int m_bEffectTime[MobEffect::NUM_EFFECTS];
    double m_effectsTimer = 0;

    void tickTimers(double now) override {
        if (now > m_effectsTimer) {
            std::shared_ptr<ServerPlayer> player = mcle_local_player();
            if (player) updateEffectsDisplay(player);
            m_effectsTimer = now + 1.0;
        }
    }

    int getSectionColumns(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionInventoryArmor: return 1;
        case eSectionInventoryInventory: return 9;
        case eSectionInventoryUsing: return 9;
        default: assert(false); return 0;
        }
    }

    int getSectionRows(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionInventoryArmor: return 4;
        case eSectionInventoryInventory: return 3;
        case eSectionInventoryUsing: return 1;
        default: assert(false); return 0;
        }
    }

    const MCLEUISlotList *sectionList(ESceneSection eSection) const {
        switch (eSection) {
        case eSectionInventoryArmor: return &m_slotListArmor;
        case eSectionInventoryInventory: return &m_slotListInventory;
        case eSectionInventoryUsing: return &m_slotListHotbar;
        default: return nullptr;
        }
    }

    void GetPositionOfSection(ESceneSection eSection, UIVec2D *pPosition) override {
        const MCLEUISlotList *list = sectionList(eSection);
        assert(list);
        pPosition->x = list ? list->rect.x : 0;
        pPosition->y = list ? list->rect.y : 0;
    }

    void GetItemScreenData(ESceneSection eSection, int iItemIndex, UIVec2D *pPosition, UIVec2D *pSize) override {
        const MCLEUISlotList *list = sectionList(eSection);
        assert(list);
        UIVec2D sectionSize;
        sectionSize.x = list ? list->rect.w : 0;
        sectionSize.y = list ? list->rect.h : 0;

        int rows = getSectionRows(eSection);
        int cols = getSectionColumns(eSection);

        pSize->x = sectionSize.x / cols;
        pSize->y = sectionSize.y / rows;

        int itemCol = iItemIndex % cols;
        int itemRow = iItemIndex / cols;

        pPosition->x = itemCol * pSize->x;
        pPosition->y = itemRow * pSize->y;
    }

    void handleSectionClick(ESceneSection eSection) override {}

    void setSectionSelectedSlot(ESceneSection eSection, int x, int y) override {
        int cols = getSectionColumns(eSection);
        int index = (y * cols) + x;
        if (MCLEUISlotList *slotList = getSection(eSection)) slotList->setHighlightSlot(index);
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        return const_cast<MCLEUISlotList *>(sectionList(eSection));
    }

    // UIScene_InventoryMenu::updateEffectsDisplay
    void updateEffectsDisplay(const std::shared_ptr<ServerPlayer> &player) {
        vector<MobEffectInstance *> *activeEffects = player->getActiveEffects();

        std::vector<MCLEUIArg> updateValues;
        for (auto &effect : *activeEffects) {
            if (effect->getDuration() >= m_bEffectTime[effect->getId()]) {
                wstring effectString = app.GetString(effect->getDescriptionId());
                if (effect->getAmplifier() > 0) {
                    wstring potencyString = L"";
                    switch (effect->getAmplifier()) {
                    case 1:
                        potencyString = L" ";
                        potencyString += app.GetString(IDS_POTION_POTENCY_1);
                        break;
                    case 2:
                        potencyString = L" ";
                        potencyString += app.GetString(IDS_POTION_POTENCY_2);
                        break;
                    case 3:
                        potencyString = L" ";
                        potencyString += app.GetString(IDS_POTION_POTENCY_3);
                        break;
                    default:
                        potencyString = app.GetString(IDS_POTION_POTENCY_0);
                        break;
                    }
                    effectString += potencyString;
                }
                int icon = 0;
                MobEffect *mobEffect = MobEffect::effects[effect->getId()];
                if (mobEffect->hasIcon()) icon = mobEffect->getIcon();
                int seconds = effect->getDuration() / SharedConstants::TICKS_PER_SECOND;
                mcle_ui_call("", "AddEffect", {mcle_ui_num(icon), mcle_ui_str(effectString), mcle_ui_num(seconds)});
            }

            if (MobEffect::effects[effect->getId()]->hasIcon()) {
                updateValues.push_back(mcle_ui_num(MobEffect::effects[effect->getId()]->getIcon()));
                updateValues.push_back(
                    mcle_ui_num(static_cast<int>(effect->getDuration() / SharedConstants::TICKS_PER_SECOND)));
            }

            m_bEffectTime[effect->getId()] = effect->getDuration();
        }

        mcle_ui_call("", "UpdateEffects", std::move(updateValues));

        delete activeEffects;
    }

    // UIControl_MinecraftPlayer::render, the player turned towards the
    // pointer. The region is in pixels already, so it's translated to rather
    // than scaled from Iggy's double size target. m_fScreenWidth /
    // m_fScreenHeight is the 16:9 screen the movie is laid out for.
    void drawPlayerPreview(const MCLECustomDrawRegion &region) {
        std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
        Minecraft *pMinecraft = Minecraft::GetInstance();
        std::shared_ptr<ServerPlayer> player = mcle_local_player();
        if (!player || !EntityRenderDispatcher::instance || !pMinecraft || !pMinecraft->options) return;
        float pointerX = m_pointerPos.x, pointerY = m_pointerPos.y;

        glLoadIdentity();
        glTranslatef(region.x0, region.y0, -2000);

        glEnable(GL_RESCALE_NORMAL);
        glEnable(GL_COLOR_MATERIAL);
        glPushMatrix();

        float width = region.x1 - region.x0;
        float height = region.y1 - region.y0;
        float xo = width / 2;
        float yo = height;

        // dynamic y offset according to region height
        glTranslatef(xo, yo - (height / 9.0f), 50.0f);

        // Base scale on height of this control
        float ss = width / (1920.0f / 1080.0f);

        glScalef(-ss, ss, ss);
        glRotatef(180, 0, 0, 1);

        float oybr = player->yBodyRot;
        float oyr = player->yRot;
        float oxr = player->xRot;
        float oyhr = player->yHeadRot;

        float xd = (m_playerRect.x + m_playerRect.w / 2) - pointerX;

        // Need to base Y on head position, not centre of mass
        float yd = (m_playerRect.y + m_playerRect.h / 2 - 40) - pointerY;

        glRotatef(45 + 90, 0, 1, 0);
        Lighting::turnOn();
        glRotatef(-45 - 90, 0, 1, 0);

        glRotatef(-(float)atan(yd / 40.0f) * 20, 1, 0, 0);

        player->yBodyRot = (float)atan(xd / 40.0f) * 20;
        player->yRot = (float)atan(xd / 40.0f) * 40;
        player->xRot = -(float)atan(yd / 40.0f) * 20;
        player->yHeadRot = player->yRot;
        glTranslatef(0, player->heightOffset, 0);
        EntityRenderDispatcher::instance->playerRotY = 180;

        // Turning on hideGui while this draws stops the name rendering
        bool wasHidingGui = pMinecraft->options->hideGui;
        pMinecraft->options->hideGui = true;
        try {
            EntityRenderDispatcher::instance->render(player, 0, 0, 0, 0, 1, false, false);
        } catch (...) {
        }
        pMinecraft->options->hideGui = wasHidingGui;

        player->yBodyRot = oybr;
        player->yRot = oyr;
        player->xRot = oxr;
        player->yHeadRot = oyhr;
        glPopMatrix();
        Lighting::turnOff();
        glDisable(GL_RESCALE_NORMAL);
    }
};

// UIController::handleInput's key repeat
const double kRepeatDelay = 0.3; // UI_REPEAT_KEY_DELAY_MS
const double kRepeatRate = 0.1;  // UI_REPEAT_KEY_REPEAT_RATE_MS

// The menu actions UIController::handleInput goes through
const int kActions[] = {
    ACTION_MENU_A,         ACTION_MENU_B,          ACTION_MENU_X,           ACTION_MENU_Y,
    ACTION_MENU_UP,        ACTION_MENU_DOWN,       ACTION_MENU_LEFT,        ACTION_MENU_RIGHT,
    ACTION_MENU_PAGEUP,    ACTION_MENU_PAGEDOWN,   ACTION_MENU_LEFT_SCROLL, ACTION_MENU_RIGHT_SCROLL,
    ACTION_MENU_PAUSEMENU, ACTION_MENU_OTHER_STICK_UP, ACTION_MENU_OTHER_STICK_DOWN,
    ACTION_MENU_OTHER_STICK_LEFT, ACTION_MENU_OTHER_STICK_RIGHT,
};
const int kActionCount = sizeof(kActions) / sizeof(kActions[0]);
double g_repeatTimer[kActionCount];

// What the world asked to open, see mcle_container_take_request
std::mutex g_requestMutex;
int g_request = MCLE_CONTAINER_NONE;
bool g_openPacket = false;
int g_openPacketType = 0;
int g_openPacketContainerId = 0;
std::wstring g_openPacketTitle;
bool g_openPacketCustomName = false;
int g_openPacketSize = 0;
int g_openContainerId = -1;

} // namespace

unsigned int mcle_container_inventory_buttons() {
    return InputManager.GetGameJoypadMaps(InputManager.GetJoypadMapVal(0), MINECRAFT_ACTION_INVENTORY);
}

// Minecraft::tick's inventory action. Riding a horse the server opens the
// horse's inventory instead (isServerControlledInventory), which isn't here.
bool mcle_container_can_open_inventory() {
    if (g_scene) return false;
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player || player->getHealth() <= 0) return false;
    if (player->isRiding() && player->riding != nullptr && player->riding->instanceof(eTYPE_HORSE)) return false;
    return true;
}

void mcle_container_open_inventory(const MCLEInventoryLayout &layout, double now) {
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player || g_scene) return;
    mcle_container_prepare();
    // Minecraft::tick plays the press unless riding
    if (!player->isRiding()) mcleContainerUI.PlayUISFX(eSFX_Press);
    mcle_container_begin(new InventoryScene(layout, player, now));
}

void mcle_container_request(int request) {
    std::lock_guard<std::mutex> lock(g_requestMutex);
    g_request = request;
}

void mcle_container_open_packet(int type, int containerId, const std::wstring &title, bool customName, int size) {
    std::lock_guard<std::mutex> lock(g_requestMutex);
    g_openPacket = true;
    g_openPacketType = type;
    g_openPacketContainerId = containerId;
    g_openPacketTitle = title;
    g_openPacketCustomName = customName;
    g_openPacketSize = size;
}

int mcle_container_open_id() {
    return g_openContainerId;
}

// ClientConnection::handleContainerOpen runs when the packet arrives, by
// which time the server has the new container on the player. The types
// without a scene here fail like a menu that couldn't open. A press that
// came in while a menu was going up is dropped.
int mcle_container_take_request() {
    bool packet, customName;
    int request, type, containerId, size;
    std::wstring title;
    {
        std::lock_guard<std::mutex> lock(g_requestMutex);
        request = g_request;
        packet = g_openPacket;
        type = g_openPacketType;
        containerId = g_openPacketContainerId;
        title = g_openPacketTitle;
        customName = g_openPacketCustomName;
        size = g_openPacketSize;
        g_request = MCLE_CONTAINER_NONE;
        g_openPacket = false;
    }
    if (packet) {
        g_openContainerId = containerId;
        if (!g_scene) {
            if (type == ContainerOpenPacket::WORKBENCH) return MCLE_CONTAINER_CRAFTING_3X3;
            if (mcle_container_take_server_packet(type, containerId, title, customName, size)) {
                return MCLE_CONTAINER_SERVER;
            }
        }
        mcle_container_open_failed();
        return MCLE_CONTAINER_NONE;
    }
    return g_scene ? MCLE_CONTAINER_NONE : request;
}

// handleContainerOpen's failed case sends a ContainerClosePacket, and the
// server's handleContainerClose closes whatever the player has open
void mcle_container_open_failed() {
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player || player->containerMenu == player->inventoryMenu) return;
    if (player->containerMenu->containerId != g_openContainerId) return;
    player->doCloseContainer();
}

void mcle_container_frame(unsigned int buttons, unsigned int prevButtons, double now) {
    if (!g_scene || g_closeRequested) return;
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    if (!mcle_local_player()) {
        g_closeRequested = true;
        return;
    }

    // UIController::handleKeyPress for each action, then UILayer::handleInput
    for (int i = 0; i < kActionCount && !g_closeRequested; ++i) {
        int action = kActions[i];
        unsigned int mapped = InputManager.GetGameJoypadMaps(InputManager.GetJoypadMapVal(0), action);
        bool down = (buttons & mapped) != 0;
        bool pressed = down && (prevButtons & mapped) == 0;
        bool released = !down && (prevButtons & mapped) != 0;
        bool repeat = false;
        if (pressed) {
            g_repeatTimer[i] = now + kRepeatDelay;
        } else if (released) {
            g_repeatTimer[i] = 0;
        } else if (down && g_repeatTimer[i] > 0 && now > g_repeatTimer[i]) {
            repeat = true;
            pressed = true;
            g_repeatTimer[i] = now + kRepeatRate;
        }
        if (!(repeat || pressed || released)) continue;
        if (repeat && !g_scene->allowRepeat(action)) continue;
        g_scene->handleInput(action, repeat, pressed, released);
    }

    if (!g_closeRequested) g_scene->tick(now);
    g_scene->snapshot();
}

std::vector<MCLEUIOp> mcle_container_take_ops() {
    std::vector<MCLEUIOp> ops;
    ops.swap(g_ops);
    return ops;
}

void mcle_container_tooltips(int out[11]) {
    for (int i = 0; i < 11; ++i) {
        out[i] = (i < eToolTipNumButtons && g_tooltipShown[i]) ? g_tooltipText[i] : -1;
    }
}

bool mcle_container_pointer(float *x, float *y) {
    *x = 0;
    *y = 0;
    return g_scene != nullptr && g_scene->pointer(x, y);
}

bool mcle_container_close_requested() {
    return g_scene != nullptr && g_closeRequested;
}

void mcle_container_close() {
    if (!g_scene) return;
    {
        std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
        g_scene->handleDestroy();
        delete g_scene;
        g_scene = nullptr;
        mcle_game_set_menu_displayed(false);
    }
    g_ops.clear();
    g_closeRequested = false;
    resetTooltips();
    for (double &t : g_repeatTimer) t = 0;
}

bool mcle_container_is_open() {
    return g_scene != nullptr;
}

void mcle_container_custom_draw(const MCLECustomDrawRegion *regions, int count) {
    if (!g_scene) return;
    if (!mcle_custom_draw_begin()) return;
    for (int i = 0; i < count; ++i) g_scene->customDraw(regions[i]);
    mcle_custom_draw_end();
}

// CMinecraftApp::GetHTMLColour
extern "C" int mcle_html_colour(int colour) {
    Minecraft *pMinecraft = Minecraft::GetInstance();
    if (!pMinecraft || !pMinecraft->skins) return 0;
    TexturePack *pack = pMinecraft->skins->getSelected();
    ColourTable *table = pack ? pack->getColourTable() : nullptr;
    if (!table) return 0;
    return static_cast<int>(table->getColour(static_cast<eMinecraftColour>(colour)));
}
