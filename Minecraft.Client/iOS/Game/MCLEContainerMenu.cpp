// The container menus. IUIScene_AbstractContainerMenu, the pointer, slot
// and tooltip logic they all share, is upstream's own: its body is copied
// out by scripts/gen-container-menu.py and compiled at the bottom of the
// includes here. IUIScene_InventoryMenu is built with the rest of the
// world. The two classes below stand in for UIScene_AbstractContainerMenu
// and UIScene_InventoryMenu, and what those do with Iggy is queued as
// MCLEUIOps for the view controller to make on the movie.

#include "MCLEContainerMenu.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "../../../upstream/Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.crafting.h"
#include "../../../upstream/Minecraft.World/FurnaceTileEntity.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.effect.h"
#include "../../../upstream/Minecraft.World/net.minecraft.stats.h"
#include "../../../upstream/Minecraft.World/HtmlString.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/SoundTypes.h"
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

std::mutex &mcle_sim_tick_mutex();                 // MCLEGameLoop.cpp
std::shared_ptr<ServerPlayer> mcle_local_player(); // MCLEGameLoop.cpp
void mcle_game_set_menu_displayed(bool displayed); // MCLEGameLoop.cpp

namespace {

std::vector<MCLEUIOp> g_ops;

// UIComponent_Tooltips as UIController::SetTooltipText / ShowTooltip leave it
int g_tooltipText[eToolTipNumButtons];
bool g_tooltipShown[eToolTipNumButtons];

bool g_closeRequested = false;

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

MCLEUIArg num(double v) { return MCLEUIArg{MCLEUIArg::Number, v, std::string()}; }
MCLEUIArg flag(bool v) { return MCLEUIArg{MCLEUIArg::Bool, v ? 1.0 : 0.0, std::string()}; }
MCLEUIArg str(const std::wstring &v) { return MCLEUIArg{MCLEUIArg::String, 0.0, toUtf8(v)}; }

void call(const std::string &path, const char *method, std::vector<MCLEUIArg> args) {
    g_ops.push_back(MCLEUIOp{MCLEUIOp::Call, path, method, std::move(args)});
}

// The UIController calls the scene logic makes
struct ContainerUI {
    void PlayUISFX(int sfx) {
        // ConsoleSoundEngine::wchUISoundNames
        static const char *const kNames[] = {"back", "craft", "craftfail", "focus", "press", "scroll"};
        if (sfx < 0 || sfx >= eSFX_MAX) return;
        g_ops.push_back(MCLEUIOp{MCLEUIOp::Sound, std::string(), kNames[sfx], {}});
    }
    void ShowTooltip(int, int button, bool show) {
        if (button >= 0 && button < eToolTipNumButtons) g_tooltipShown[button] = show;
    }
    void SetTooltipText(int, int button, int ids) {
        if (button >= 0 && button < eToolTipNumButtons) g_tooltipText[button] = ids;
    }
    bool IsTutorialVisible(int) { return false; }
    // The strip animates from the keys Ruffle gets from the pad
    template <class... A> void AnimateKeyPress(A...) {}
    // ui.SetTooltips(iPad, -1): every button set to nothing
    template <class... A> void SetTooltips(A...) {
        for (int i = 0; i < eToolTipNumButtons; ++i) g_tooltipShown[i] = false;
    }
    void NavigateBack(int) { g_closeRequested = true; }
    void CloseUIScenes(int) { g_closeRequested = true; }
    template <class... A> bool NavigateToScene(A...) { return false; }
};
ContainerUI mcleContainerUI;

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

// UIControl_SlotList: the calls it makes on its value path. x, y, width and
// height are what setupControl read off the movie.
struct SlotList {
    std::string path;
    MCLEControlRect rect{};
    int lastHighlighted = 0;

    explicit SlotList(const char *p) : path(p) {}

    void addSlot(int id) { call(path, "addSlot", {num(id), flag(false), flag(false)}); }
    void addSlots(int start, int count) {
        for (int i = start; i < start + count; ++i) addSlot(i);
    }
    void setSlotHighlighted(int index, bool highlight) {
        call(path, "SetSlotHighlight", {num(index), flag(highlight)});
    }
    void setHighlightSlot(int index) {
        if (index != lastHighlighted) {
            if (lastHighlighted != -1) setSlotHighlighted(lastHighlighted, false);
            setSlotHighlighted(index, true);
            lastHighlighted = index;
        }
    }
    void setFocus(bool focus) {
        if (lastHighlighted != -1) setSlotHighlighted(lastHighlighted, focus);
    }
};

// UIScene_AbstractContainerMenu
class ContainerScene : public virtual IUIScene_AbstractContainerMenu {
public:
    ContainerScene() : m_slotListHotbar("MainPanel.hotbarList"), m_slotListInventory("MainPanel.inventoryList") {}

    int getPad() override { return 0; }
    int getMovieWidth() override { return 1920; }
    int getMovieHeight() override { return 1080; }
    void getMouseToSWFScale(float &scaleX, float &scaleY) override {
        scaleX = 1.0f;
        scaleY = 1.0f;
    }

    // UIScene_AbstractContainerMenu::handleInput, for a press
    void input(int key, bool repeat) { handleKeyDown(getPad(), key, repeat); }

    // UIScene_AbstractContainerMenu::tick. UIScene::tick runs the timers
    // and sends any label that changed, then the pointer moves.
    void tick(double now) {
        tickTimers(now);
        // UIControl_Base::tick for the cursor's label
        if (m_cursorLabelChanged) {
            m_cursorLabelChanged = false;
            call("MainPanel.cursor", "SetLabel", {str(m_cursorLabel)});
        }
        onMouseTick();
    }

    void pointer(float *x, float *y) {
        *x = m_pointerPos.x;
        *y = m_pointerPos.y;
    }

    AbstractContainerMenu *menu() { return m_menu; }

    // UIScene_AbstractContainerMenu::handleDestroy. There's no tutorial
    // state to put back.
    void destroy() {
        std::shared_ptr<ServerPlayer> player = mcle_local_player();
        if (player != nullptr && player->containerMenu != nullptr && m_menu != nullptr &&
            player->containerMenu->containerId == m_menu->containerId) {
            player->closeContainer();
        }
    }

    MCLEControlRect m_backgroundPanel{};

protected:
    SlotList m_slotListHotbar, m_slotListInventory;
    ESceneSection m_focusSection = eSectionNone;
    std::wstring m_cursorLabel;
    bool m_cursorLabelChanged = false;

    virtual void tickTimers(double now) {}
    virtual SlotList *getSection(ESceneSection eSection) { return nullptr; }

    void InitDataAssociations(int iPad, AbstractContainerMenu *menu, int startIndex) override {}

    void PlatformInitialize(int iPad, int startIndex) override {
        call("MainPanel.inventoryLabel", "Init", {str(app.GetString(IDS_INVENTORY))});

        if (startIndex >= 0) {
            m_slotListInventory.addSlots(startIndex, 27);
            m_slotListHotbar.addSlots(startIndex + 27, 9);
        }

        // Min and max extents for the pointer, it needs to be able to move off
        // the container to drop items
        float fPointerWidth = 50;
        float fPointerHeight = 50;

        float fPanelWidth = m_backgroundPanel.w;
        float fPanelHeight = m_backgroundPanel.h;
        float fPanelX = m_backgroundPanel.x;
        float fPanelY = m_backgroundPanel.y;
        m_fPointerImageOffsetX = 0;
        m_fPointerImageOffsetY = 0;

        m_fPanelMinX = fPanelX;
        m_fPanelMaxX = fPanelX + fPanelWidth;
        m_fPanelMinY = fPanelY;
        m_fPanelMaxY = fPanelY + fPanelHeight;

        // One local player
        m_fPointerMinY = fPanelY - fPointerHeight;
        m_fPointerMinX = fPanelX - fPointerWidth;
        m_fPointerMaxX = m_fPanelMaxX + fPointerWidth;
        m_fPointerMaxY = m_fPanelMaxY + (fPointerHeight / 2);

        // Put the pointer over first item in use row to start with
        UIVec2D itemPos;
        UIVec2D itemSize;
        GetItemScreenData(m_eCurrSection, 0, &(itemPos), &(itemSize));

        UIVec2D sectionPos;
        GetPositionOfSection(m_eCurrSection, &(sectionPos));

        UIVec2D vPointerPos = sectionPos;
        vPointerPos += itemPos;
        vPointerPos.x += (itemSize.x / 2.0f);
        vPointerPos.y += (itemSize.y / 2.0f);

        vPointerPos.x -= m_fPointerImageOffsetX;
        vPointerPos.y -= m_fPointerImageOffsetY;

        m_pointerPos = vPointerPos;
    }

    bool doesSectionTreeHaveFocus(ESceneSection eSection) override { return false; }

    void setSectionFocus(ESceneSection eSection, int iPad) override {
        SlotList *newFocus = getSection(eSection);
        if (newFocus) newFocus->setFocus(true);

        if (m_focusSection != eSectionNone) {
            SlotList *currentFocus = getSection(m_focusSection);
            if (currentFocus != newFocus && currentFocus) currentFocus->setFocus(false);
        }
        m_focusSection = eSection;
    }

    void setFocusToPointer(int iPad) override {
        if (m_focusSection != eSectionNone) {
            SlotList *currentFocus = getSection(m_focusSection);
            if (currentFocus) currentFocus->setFocus(false);
        }
        m_focusSection = eSectionNone;
    }

    // m_cursorPath.setLabel(HtmlString::Compose(description), false, newSlot)
    void SetPointerText(vector<HtmlString> *description, bool newSlot) override {
        std::wstring label = HtmlString::Compose(description);
        if (newSlot || ((!m_cursorLabel.empty() || !label.empty()) && m_cursorLabel.compare(label) != 0)) {
            m_cursorLabelChanged = true;
        }
        m_cursorLabel = label;
    }

    shared_ptr<ItemInstance> getSlotItem(ESceneSection eSection, int iSlot) override {
        Slot *slot = m_menu->getSlot(getSectionStartOffset(eSection) + iSlot);
        if (slot) return slot->getItem();
        return nullptr;
    }

    Slot *getSlot(ESceneSection eSection, int iSlot) override {
        return m_menu->getSlot(getSectionStartOffset(eSection) + iSlot);
    }

    bool isSlotEmpty(ESceneSection eSection, int iSlot) override {
        Slot *slot = m_menu->getSlot(getSectionStartOffset(eSection) + iSlot);
        if (slot) return !slot->hasItem();
        return false;
    }

    // Handled by AS
    void adjustPointerForSafeZone() override {}
};

// UIScene_InventoryMenu
class InventoryScene : public ContainerScene, public IUIScene_InventoryMenu {
public:
    InventoryScene(const MCLEInventoryLayout &layout, const std::shared_ptr<ServerPlayer> &player, double now)
        : m_slotListArmor("MainPanel.armorList") {
        m_backgroundPanel = layout.backgroundPanel;
        m_slotListHotbar.rect = layout.hotbarList;
        m_slotListInventory.rect = layout.inventoryList;
        m_slotListArmor.rect = layout.armorList;

        InventoryMenu *menu = static_cast<InventoryMenu *>(player->inventoryMenu);

        player->awardStat(GenericStats::openInventory(), GenericStats::param_openInventory());

        Initialize(0, menu, false, InventoryMenu::INV_SLOT_START, eSectionInventoryUsing, eSectionInventoryMax, false);

        m_slotListArmor.addSlots(InventoryMenu::ARMOR_SLOT_START,
                                 InventoryMenu::ARMOR_SLOT_END - InventoryMenu::ARMOR_SLOT_START);

        for (unsigned int i = 0; i < MobEffect::NUM_EFFECTS; ++i) m_bEffectTime[i] = 0;

        updateEffectsDisplay(player);
        // addTimer(INVENTORY_UPDATE_EFFECTS_TIMER_ID, 1000)
        m_effectsTimer = now + 1.0;
    }

protected:
    SlotList m_slotListArmor;
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

    const SlotList *sectionList(ESceneSection eSection) const {
        switch (eSection) {
        case eSectionInventoryArmor: return &m_slotListArmor;
        case eSectionInventoryInventory: return &m_slotListInventory;
        case eSectionInventoryUsing: return &m_slotListHotbar;
        default: return nullptr;
        }
    }

    void GetPositionOfSection(ESceneSection eSection, UIVec2D *pPosition) override {
        const SlotList *list = sectionList(eSection);
        assert(list);
        pPosition->x = list ? list->rect.x : 0;
        pPosition->y = list ? list->rect.y : 0;
    }

    void GetItemScreenData(ESceneSection eSection, int iItemIndex, UIVec2D *pPosition, UIVec2D *pSize) override {
        const SlotList *list = sectionList(eSection);
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
        if (SlotList *slotList = getSection(eSection)) slotList->setHighlightSlot(index);
    }

    SlotList *getSection(ESceneSection eSection) override { return const_cast<SlotList *>(sectionList(eSection)); }

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
                call("", "AddEffect", {num(icon), str(effectString), num(seconds)});
            }

            if (MobEffect::effects[effect->getId()]->hasIcon()) {
                updateValues.push_back(num(MobEffect::effects[effect->getId()]->getIcon()));
                updateValues.push_back(num(static_cast<int>(effect->getDuration() / SharedConstants::TICKS_PER_SECOND)));
            }

            m_bEffectTime[effect->getId()] = effect->getDuration();
        }

        call("", "UpdateEffects", std::move(updateValues));

        delete activeEffects;
    }
};

ContainerScene *g_scene = nullptr;
// The inventory's iggy_player control, MainPanel coordinates
MCLEControlRect g_playerRect{};

// What the slots held at the end of the last frame, for drawing
std::vector<std::shared_ptr<ItemInstance>> g_slotItems;
std::vector<bool> g_slotValid;
std::shared_ptr<ItemInstance> g_carried;
bool g_carriedValid = true;

std::shared_ptr<ItemInstance> copyOf(const std::shared_ptr<ItemInstance> &item) {
    return item != nullptr ? item->copy() : nullptr;
}

void snapshotSlots() {
    g_slotItems.clear();
    g_slotValid.clear();
    g_carried = nullptr;
    if (!g_scene || !g_scene->menu()) return;
    AbstractContainerMenu *menu = g_scene->menu();
    unsigned int count = menu->getSize();
    for (unsigned int i = 0; i < count; ++i) {
        Slot *slot = menu->getSlot(i);
        std::shared_ptr<ItemInstance> item = slot ? slot->getItem() : nullptr;
        g_slotItems.push_back(copyOf(item));
        g_slotValid.push_back(item == nullptr || menu->isValidIngredient(item, i));
    }
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (player) {
        std::shared_ptr<ItemInstance> carried = player->inventory->getCarried();
        g_carried = copyOf(carried);
        g_carriedValid = carried == nullptr || menu->isValidIngredient(carried, -1);
    }
}

void resetTooltips() {
    for (int i = 0; i < eToolTipNumButtons; ++i) {
        g_tooltipText[i] = -1;
        g_tooltipShown[i] = false;
    }
}

// UIControl_MinecraftPlayer::render, the player turned towards the pointer.
// The region is in pixels already, so it's translated to rather than
// scaled from Iggy's double size target. m_fScreenWidth / m_fScreenHeight
// is the 16:9 screen the movie is laid out for.
void drawPlayerPreview(const MCLECustomDrawRegion &region) {
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    Minecraft *pMinecraft = Minecraft::GetInstance();
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player || !g_scene || !EntityRenderDispatcher::instance || !pMinecraft || !pMinecraft->options) return;
    float pointerX = 0, pointerY = 0;
    g_scene->pointer(&pointerX, &pointerY);

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

    float xd = (g_playerRect.x + g_playerRect.w / 2) - pointerX;

    // Need to base Y on head position, not centre of mass
    float yd = (g_playerRect.y + g_playerRect.h / 2 - 40) - pointerY;

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
    g_ops.clear();
    resetTooltips();
    g_closeRequested = false;
    // Minecraft::tick plays the press unless riding
    if (!player->isRiding()) mcleContainerUI.PlayUISFX(eSFX_Press);
    g_playerRect = layout.player;
    g_scene = new InventoryScene(layout, player, now);
    mcle_game_set_menu_displayed(true);
    snapshotSlots();
}

void mcle_container_frame(unsigned int buttons, unsigned int prevButtons, double now) {
    if (!g_scene || g_closeRequested) return;
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    if (!mcle_local_player()) {
        g_closeRequested = true;
        return;
    }

    // UIController::handleInput, the menu actions newly pressed this frame
    static const int kActions[] = {
        ACTION_MENU_A,         ACTION_MENU_B,          ACTION_MENU_X,           ACTION_MENU_Y,
        ACTION_MENU_UP,        ACTION_MENU_DOWN,       ACTION_MENU_LEFT,        ACTION_MENU_RIGHT,
        ACTION_MENU_PAGEUP,    ACTION_MENU_PAGEDOWN,   ACTION_MENU_LEFT_SCROLL, ACTION_MENU_RIGHT_SCROLL,
        ACTION_MENU_PAUSEMENU, ACTION_MENU_OTHER_STICK_UP, ACTION_MENU_OTHER_STICK_DOWN,
        ACTION_MENU_OTHER_STICK_LEFT, ACTION_MENU_OTHER_STICK_RIGHT,
    };
    const unsigned int pressed = buttons & ~prevButtons;
    for (int action : kActions) {
        if (g_closeRequested) break;
        unsigned int mapped = InputManager.GetGameJoypadMaps(InputManager.GetJoypadMapVal(0), action);
        if (pressed & mapped) g_scene->input(action, false);
    }

    if (!g_closeRequested) g_scene->tick(now);
    snapshotSlots();
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

void mcle_container_pointer(float *x, float *y) {
    *x = 0;
    *y = 0;
    if (g_scene) g_scene->pointer(x, y);
}

bool mcle_container_close_requested() {
    return g_scene != nullptr && g_closeRequested;
}

void mcle_container_close() {
    if (!g_scene) return;
    {
        std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
        g_scene->destroy();
        delete g_scene;
        g_scene = nullptr;
        mcle_game_set_menu_displayed(false);
    }
    g_ops.clear();
    g_closeRequested = false;
    resetTooltips();
    g_slotItems.clear();
    g_slotValid.clear();
    g_carried = nullptr;
}

bool mcle_container_is_open() {
    return g_scene != nullptr;
}

void mcle_container_custom_draw(const MCLECustomDrawRegion *regions, int count) {
    if (!g_scene) return;
    if (!mcle_custom_draw_begin()) return;
    for (int i = 0; i < count; ++i) {
        // UIScene_InventoryMenu::customDraw, then
        // UIScene_AbstractContainerMenu::customDraw
        std::shared_ptr<ItemInstance> item = nullptr;
        bool valid = true;
        int slotId = -1;
        if (strcmp(regions[i].name, "player") == 0) {
            drawPlayerPreview(regions[i]);
            continue;
        }
        if (strcmp(regions[i].name, "pointerIcon") == 0) {
            item = g_carried;
            valid = g_carriedValid;
        } else if (sscanf(regions[i].name, "slot_%d", &slotId) == 1 && slotId >= 0 &&
                   slotId < static_cast<int>(g_slotItems.size())) {
            item = g_slotItems[slotId];
            valid = g_slotValid[slotId];
        }
        if (item != nullptr) mcle_custom_draw_slot(regions[i], item, valid ? 1.0f : 0.5f, true);
    }
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
