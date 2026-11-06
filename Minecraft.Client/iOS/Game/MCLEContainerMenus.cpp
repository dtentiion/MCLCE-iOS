// The containers the server opens on the player, the cases of
// ClientConnection::handleContainerOpen. The client opens its own copy of
// the container and a menu over it; here the scene runs on the menu the
// server put on the player, which that copy would mirror. Each scene
// stands in for its UIScene_ class.

#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"
#include "MCLEAbstractContainerScene.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>

#include "../../../upstream/Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.h"
#include "../../../upstream/Minecraft.World/FurnaceTileEntity.h"
#include "../../../upstream/Minecraft.World/BrewingStandTileEntity.h"
#include "../../../upstream/Minecraft.World/BeaconTileEntity.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.effect.h"
#include "../../../upstream/Minecraft.World/PotionBrewing.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/Random.h"
#include "../../../upstream/Minecraft.World/EnchantmentMenu.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/Font.h"
#include "../../../upstream/Minecraft.Client/Textures.h"
#include "../../../upstream/Minecraft.Client/Lighting.h"
#include "../../../upstream/Minecraft.Client/BookModel.h"
#include "../../../upstream/Minecraft.World/ContainerOpenPacket.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_ContainerMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_FurnaceMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_DispenserMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_HopperMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_BrewingMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_EnchantingMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_BeaconMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_AnvilMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_HorseInventoryMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_FireworksMenu.h"
#include "../../../upstream/Minecraft.Client/EntityRenderDispatcher.h"
#include "../../../upstream/Minecraft.Client/Options.h"
#include "../../../upstream/Minecraft.World/InputOutputStream.h"

// The anvil's player, the local player's ServerPlayer
static std::shared_ptr<Player> mcle_container_player() {
    return mcle_local_player();
}

// PlayerConnection::handleCustomPayload's SET_ITEM_NAME_PACKET. The server
// is this same player, so the name goes straight to its menu.
static void mcle_anvil_set_item_name(const std::wstring &name) {
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player) return;
    AnvilMenu *menu = dynamic_cast<AnvilMenu *>(player->containerMenu);
    if (menu) {
        if (name.length() <= 30) menu->setItemName(name);
    }
}

// IUIScene_AnvilMenu.cpp, see scripts/gen-anvil-menu.py
#include "../../../upstream/Minecraft.Client/iOS_AnvilMenu.inc"

// PlayerConnection::handleCustomPayload's SET_BEACON_PACKET, on the same
// player's menu
static void mcle_beacon_set_powers(int primary, int secondary) {
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player) return;
    if (dynamic_cast<BeaconMenu *>(player->containerMenu) != nullptr) {
        BeaconMenu *beaconMenu = static_cast<BeaconMenu *>(player->containerMenu);
        Slot *slot = beaconMenu->getSlot(0);
        if (slot->hasItem()) {
            slot->remove(1);
            shared_ptr<BeaconTileEntity> beacon = beaconMenu->getBeacon();
            beacon->setPrimaryPower(primary);
            beacon->setSecondaryPower(secondary);
            beacon->setChanged();
        }
    }
}

// IUIScene_BeaconMenu.cpp, see scripts/gen-beacon-menu.py
#include "../../../upstream/Minecraft.Client/iOS_BeaconMenu.inc"

namespace {

// The ContainerOpenPacket the scene is for
struct OpenPacket {
    int type = 0;
    int containerId = -1;
    std::wstring title;
    bool customName = false;
    int size = 0;
    int entityId = 0;
};
OpenPacket g_packet;

MCLEControlRect (*g_rectReader)(const char *path) = nullptr;

// UIControl::getXPos and friends, read when the scene maps the control
MCLEControlRect rectAt(const char *path) {
    return g_rectReader ? g_rectReader(path) : MCLEControlRect{0, 0, 0, 0};
}

bool isChest(int type) {
    switch (type) {
    case ContainerOpenPacket::BONUS_CHEST:
    case ContainerOpenPacket::LARGE_CHEST:
    case ContainerOpenPacket::ENDER_CHEST:
    case ContainerOpenPacket::CONTAINER:
    case ContainerOpenPacket::MINECART_CHEST:
        return true;
    }
    return false;
}

bool hasScene(int type) {
    switch (type) {
    case ContainerOpenPacket::FURNACE:
    case ContainerOpenPacket::TRAP:
    case ContainerOpenPacket::DROPPER:
    case ContainerOpenPacket::HOPPER:
    case ContainerOpenPacket::BREWING_STAND:
    case ContainerOpenPacket::ENCHANTMENT:
    case ContainerOpenPacket::BEACON:
    case ContainerOpenPacket::REPAIR_TABLE:
    case ContainerOpenPacket::TRADER_NPC:
    case ContainerOpenPacket::HORSE:
    case ContainerOpenPacket::FIREWORKS:
        return true;
    }
    return isChest(type);
}

// A scene over a container the server opened. ServerPlayer::tick closes it
// when it stops being valid, and ClientConnection::handleContainerClose
// closes the menu after it. The sections are all slot lists, laid out the
// way every UIScene_ container menu lays them out.
class ServerContainerScene : public MCLEAbstractContainerScene {
public:
    void tick(double now) override {
        std::shared_ptr<ServerPlayer> player = mcle_local_player();
        if (player && player->containerMenu != m_menu) {
            // LocalPlayer::closeContainer
            mcleContainerUI.PlayUISFX(eSFX_Back);
            mcleContainerUI.CloseUIScenes(getPad());
            return;
        }
        MCLEAbstractContainerScene::tick(now);
    }

protected:
    void mapSharedControls() {
        m_backgroundPanel = rectAt("MainPanel.BackgroundPanel");
        m_slotListInventory.rect = rectAt("MainPanel.inventoryList");
        m_slotListHotbar.rect = rectAt("MainPanel.hotbarList");
    }

    void mapSlotList(MCLEUISlotList &list) { list.rect = rectAt(list.path.c_str()); }

    void GetPositionOfSection(ESceneSection eSection, UIVec2D *pPosition) override {
        MCLEUISlotList *list = getSection(eSection);
        pPosition->x = list ? list->rect.x : 0;
        pPosition->y = list ? list->rect.y : 0;
    }

    void GetItemScreenData(ESceneSection eSection, int iItemIndex, UIVec2D *pPosition, UIVec2D *pSize) override {
        MCLEUISlotList *list = getSection(eSection);
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
};

// UIScene_ContainerMenu
class ChestScene : public ServerContainerScene, public IUIScene_ContainerMenu {
public:
    ChestScene(AbstractContainerMenu *menu, const std::wstring &name)
        : m_slotListContainer("MainPanel.containerList"), m_labelChest("MainPanel.chestLabel") {
        mapSharedControls();
        mapSlotList(m_slotListContainer);

        m_labelChest.init(name);

        enterTutorialState(e_Tutorial_State_Container_Menu);

        int containerSize = menu->getSize() - (27 + 9);

        Initialize(0, menu, false, containerSize, eSectionContainerUsing, eSectionContainerMax);

        m_slotListContainer.addSlots(0, containerSize);
    }

protected:
    MCLEUISlotList m_slotListContainer;
    MCLEUILabel m_labelChest;

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionContainerChest: cols = 9; break;
        case eSectionContainerInventory: cols = 9; break;
        case eSectionContainerUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionContainerChest: rows = (m_menu->getSize() - (27 + 9)) / 9; break;
        case eSectionContainerInventory: rows = 3; break;
        case eSectionContainerUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionContainerChest: return &m_slotListContainer;
        case eSectionContainerInventory: return &m_slotListInventory;
        case eSectionContainerUsing: return &m_slotListHotbar;
        default: assert(false); return nullptr;
        }
    }
};

// UIScene_FurnaceMenu. m_furnace is the client's copy, kept up to date by
// the server's ContainerSetData; here it's the server's own.
class FurnaceScene : public ServerContainerScene, public IUIScene_FurnaceMenu {
public:
    FurnaceScene(AbstractContainerMenu *menu, const std::shared_ptr<FurnaceTileEntity> &furnace)
        : m_furnace(furnace), m_slotListFuel("MainPanel.Fuel"), m_slotListIngredient("MainPanel.Ingredient"),
          m_slotListResult("MainPanel.Result"), m_labelFurnace("MainPanel.Furnace_text"),
          m_labelIngredient("MainPanel.Ingredient_Label"), m_labelFuel("MainPanel.Fuel_Label"),
          m_progressFurnaceFire("MainPanel.FurnaceFire"), m_progressFurnaceArrow("MainPanel.FurnaceArrow") {
        mapSharedControls();
        mapSlotList(m_slotListFuel);
        mapSlotList(m_slotListIngredient);
        mapSlotList(m_slotListResult);

        m_labelFurnace.init(m_furnace->getName());
        m_labelIngredient.init(app.GetString(IDS_INGREDIENT));
        m_labelFuel.init(app.GetString(IDS_FUEL));
        m_progressFurnaceFire.init(L"", 0, 0, 12, 0);
        m_progressFurnaceArrow.init(L"", 0, 0, 24, 0);

        enterTutorialState(e_Tutorial_State_Furnace_Menu);

        Initialize(0, menu, false, FurnaceMenu::INV_SLOT_START, eSectionFurnaceUsing, eSectionFurnaceMax);

        m_slotListFuel.addSlots(FurnaceMenu::FUEL_SLOT, 1);
        m_slotListIngredient.addSlots(FurnaceMenu::INGREDIENT_SLOT, 1);
        m_slotListResult.addSlots(FurnaceMenu::RESULT_SLOT, 1);
    }

    void tick(double now) override {
        m_progressFurnaceFire.setProgress(m_furnace->getLitProgress(12));
        m_progressFurnaceArrow.setProgress(m_furnace->getBurnProgress(24));
        ServerContainerScene::tick(now);
    }

protected:
    std::shared_ptr<FurnaceTileEntity> m_furnace;
    MCLEUISlotList m_slotListFuel, m_slotListIngredient, m_slotListResult;
    MCLEUILabel m_labelFurnace, m_labelIngredient, m_labelFuel;
    MCLEUIProgress m_progressFurnaceFire, m_progressFurnaceArrow;

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionFurnaceResult: cols = 1; break;
        case eSectionFurnaceFuel: cols = 1; break;
        case eSectionFurnaceIngredient: cols = 1; break;
        case eSectionFurnaceInventory: cols = 9; break;
        case eSectionFurnaceUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionFurnaceResult: rows = 1; break;
        case eSectionFurnaceFuel: rows = 1; break;
        case eSectionFurnaceIngredient: rows = 1; break;
        case eSectionFurnaceInventory: rows = 3; break;
        case eSectionFurnaceUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionFurnaceResult: return &m_slotListResult;
        case eSectionFurnaceFuel: return &m_slotListFuel;
        case eSectionFurnaceIngredient: return &m_slotListIngredient;
        case eSectionFurnaceInventory: return &m_slotListInventory;
        case eSectionFurnaceUsing: return &m_slotListHotbar;
        default: assert(false); return nullptr;
        }
    }
};

// UIScene_DispenserMenu, for dispensers and droppers
class TrapScene : public ServerContainerScene, public IUIScene_DispenserMenu {
public:
    TrapScene(AbstractContainerMenu *menu, const std::shared_ptr<Container> &trap)
        : m_slotListTrap("MainPanel.Trap"), m_labelDispenser("MainPanel.dispenserLabel") {
        mapSharedControls();
        mapSlotList(m_slotListTrap);

        m_labelDispenser.init(trap->getName());

        enterTutorialState(e_Tutorial_State_Trap_Menu);

        int containerSize = trap->getContainerSize();
        Initialize(0, menu, false, containerSize, eSectionTrapUsing, eSectionTrapMax);

        m_slotListTrap.addSlots(0, 9);
    }

protected:
    MCLEUISlotList m_slotListTrap;
    MCLEUILabel m_labelDispenser;

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionTrapTrap: cols = 3; break;
        case eSectionTrapInventory: cols = 9; break;
        case eSectionTrapUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionTrapTrap: rows = 3; break;
        case eSectionTrapInventory: rows = 3; break;
        case eSectionTrapUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionTrapTrap: return &m_slotListTrap;
        case eSectionTrapInventory: return &m_slotListInventory;
        case eSectionTrapUsing: return &m_slotListHotbar;
        default: assert(false); return nullptr;
        }
    }
};

// UIScene_HopperMenu, for hoppers and hopper minecarts. Like upstream it
// asks the five slot list for nine slots.
class HopperScene : public ServerContainerScene, public IUIScene_HopperMenu {
public:
    HopperScene(AbstractContainerMenu *menu, const std::shared_ptr<Container> &hopper)
        : m_slotListTrap("MainPanel.Trap"), m_labelDispenser("MainPanel.dispenserLabel") {
        mapSharedControls();
        mapSlotList(m_slotListTrap);

        m_labelDispenser.init(hopper->getName());

        enterTutorialState(e_Tutorial_State_Hopper_Menu);

        int containerSize = hopper->getContainerSize();
        Initialize(0, menu, false, containerSize, eSectionHopperUsing, eSectionHopperMax);

        m_slotListTrap.addSlots(0, 9);
    }

protected:
    MCLEUISlotList m_slotListTrap;
    MCLEUILabel m_labelDispenser;

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionHopperContents: cols = 5; break;
        case eSectionHopperInventory: cols = 9; break;
        case eSectionHopperUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionHopperContents: rows = 1; break;
        case eSectionHopperInventory: rows = 3; break;
        case eSectionHopperUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionHopperContents: return &m_slotListTrap;
        case eSectionHopperInventory: return &m_slotListInventory;
        case eSectionHopperUsing: return &m_slotListHotbar;
        default: assert(false); return nullptr;
        }
    }
};

// UIScene_BrewingStandMenu, over the server's brewing stand like the
// furnace
class BrewingScene : public ServerContainerScene, public IUIScene_BrewingMenu {
public:
    BrewingScene(AbstractContainerMenu *menu, const std::shared_ptr<BrewingStandTileEntity> &brewingStand)
        : m_brewingStand(brewingStand),
          m_slotListBottles{MCLEUISlotList("MainPanel.Bottle1"), MCLEUISlotList("MainPanel.Bottle2"),
                            MCLEUISlotList("MainPanel.Bottle3")},
          m_slotListIngredient("MainPanel.Ingredient"), m_labelBrewingStand("MainPanel.BrewingStandText"),
          m_progressBrewingArrow("MainPanel.BrewingArrow"), m_progressBrewingBubbles("MainPanel.BrewingBubbles") {
        mapSharedControls();
        for (MCLEUISlotList &list : m_slotListBottles) mapSlotList(list);
        mapSlotList(m_slotListIngredient);

        m_progressBrewingArrow.init(L"", 0, 0, PotionBrewing::BREWING_TIME_SECONDS * SharedConstants::TICKS_PER_SECOND,
                                    0);
        m_progressBrewingBubbles.init(L"", 0, 0, 30, 0);

        m_labelBrewingStand.init(m_brewingStand->getName());

        enterTutorialState(e_Tutorial_State_Brewing_Menu);

        Initialize(0, menu, false, BrewingStandMenu::INV_SLOT_START, eSectionBrewingUsing, eSectionBrewingMax);

        m_slotListIngredient.addSlots(BrewingStandMenu::INGREDIENT_SLOT, 1);
        for (unsigned int i = 0; i < 3; ++i) {
            m_slotListBottles[i].addSlots(BrewingStandMenu::BOTTLE_SLOT_START + i, 1);
        }
    }

    void tick(double now) override {
        m_progressBrewingArrow.setProgress(m_brewingStand->getBrewTime());

        int value = 0;
        int bubbleStep = (m_brewingStand->getBrewTime() / 2) % 7;
        switch (bubbleStep) {
        case 0: value = 0; break;
        case 6: value = 5; break;
        case 5: value = 10; break;
        case 4: value = 15; break;
        case 3: value = 20; break;
        case 2: value = 25; break;
        case 1: value = 30; break;
        }
        m_progressBrewingBubbles.setProgress(value);
        ServerContainerScene::tick(now);
    }

protected:
    std::shared_ptr<BrewingStandTileEntity> m_brewingStand;
    MCLEUISlotList m_slotListBottles[3], m_slotListIngredient;
    MCLEUILabel m_labelBrewingStand;
    MCLEUIProgress m_progressBrewingArrow, m_progressBrewingBubbles;

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionBrewingBottle1: cols = 1; break;
        case eSectionBrewingBottle2: cols = 1; break;
        case eSectionBrewingBottle3: cols = 1; break;
        case eSectionBrewingIngredient: cols = 1; break;
        case eSectionBrewingInventory: cols = 9; break;
        case eSectionBrewingUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionBrewingBottle1: rows = 1; break;
        case eSectionBrewingBottle2: rows = 1; break;
        case eSectionBrewingBottle3: rows = 1; break;
        case eSectionBrewingIngredient: rows = 1; break;
        case eSectionBrewingInventory: rows = 3; break;
        case eSectionBrewingUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionBrewingBottle1: return &m_slotListBottles[0];
        case eSectionBrewingBottle2: return &m_slotListBottles[1];
        case eSectionBrewingBottle3: return &m_slotListBottles[2];
        case eSectionBrewingIngredient: return &m_slotListIngredient;
        case eSectionBrewingInventory: return &m_slotListInventory;
        case eSectionBrewingUsing: return &m_slotListHotbar;
        default: assert(false); return nullptr;
        }
    }
};

// UIControl_EnchantmentButton::EnchantmentNames
struct EnchantmentNames {
    Random random;
    std::vector<std::wstring> words;

    EnchantmentNames() {
        std::wstring allWords =
            L"the elder scrolls klaatu berata niktu xyzzy bless curse light darkness fire air earth water hot dry "
            L"cold wet ignite snuff embiggen twist shorten stretch fiddle destroy imbue galvanize enchant free "
            L"limited range of towards inside sphere cube self other ball mental physical grow shrink demon "
            L"elemental spirit animal creature beast humanoid undead fresh stale ";
        std::wistringstream iss(allWords);
        std::copy(std::istream_iterator<std::wstring, wchar_t, std::char_traits<wchar_t>>(iss),
                  std::istream_iterator<std::wstring, wchar_t, std::char_traits<wchar_t>>(),
                  std::back_inserter(words));
    }

    std::wstring getRandomName() {
        int wordCount = random.nextInt(2) + 3;
        std::wstring word = L"";
        for (int i = 0; i < wordCount; i++) {
            if (i > 0) word += L" ";
            word += words[random.nextInt(static_cast<int>(words.size()))];
        }
        return word;
    }

    static EnchantmentNames &instance() {
        static EnchantmentNames names;
        return names;
    }
};

// UIControl_EnchantmentButton
struct EnchantmentButton {
    enum EState { eState_Inactive = 0, eState_Active = 1, eState_Selected = 2 };

    MCLEUILabel label;
    MCLEControlRect rect{};
    int index;
    EState lastState = eState_Inactive;
    int lastCost = 0;
    std::wstring enchantmentString;
    bool hasFocus = false;
    unsigned int textColour, textFocusColour, textDisabledColour;

    EnchantmentButton(const char *path, int i)
        : label(path), index(i), textColour(app.GetHTMLColour(eTextColor_Enchant)),
          textFocusColour(app.GetHTMLColour(eTextColor_EnchantFocus)),
          textDisabledColour(app.GetHTMLColour(eTextColor_EnchantDisabled)) {}

    void updateState(EnchantmentMenu *menu, const std::shared_ptr<ServerPlayer> &player) {
        EState state = eState_Inactive;
        int cost = menu->costs[index];
        if (cost > player->experienceLevel && !player->abilities.instabuild) {
            // Dark background
            state = eState_Inactive;
        } else {
            // Light background and focus background
            state = hasFocus ? eState_Selected : eState_Active;
        }

        if (cost != lastCost) {
            label.setLabel(std::to_wstring(cost));
            lastCost = cost;
            enchantmentString = EnchantmentNames::instance().getRandomName();
        }
        if (cost == 0) {
            // Dark background
            state = eState_Inactive;
            label.setLabel(L"");
        }

        if (state != lastState) {
            mcle_ui_call(label.path, "ChangeState", {mcle_ui_num(static_cast<int>(state))});
            lastState = state;
        }
    }

    // UIControl_EnchantmentButton::render, the made up name in the
    // enchanting table's font. The region is in pixels already.
    void render(const MCLECustomDrawRegion &region, EnchantmentMenu *menu, const std::shared_ptr<ServerPlayer> &player) {
        float width = region.x1 - region.x0;
        float height = region.y1 - region.y0;
        if (rect.w <= 0 || rect.h <= 0) return;

        glLoadIdentity();
        glTranslatef(region.x0, region.y0, -2000);

        // Revert the scale from the setup
        float ssX = width / rect.w;
        float ssY = height / rect.h;
        glScalef(ssX, ssY, 1.0f);

        // eSceneResolution_1080
        float ss = 3.0f;
        glScalef(ss, ss, ss);

        int cost = menu->costs[index];

        glColor4f(1, 1, 1, 1);
        if (cost != 0) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.1f);
            Minecraft *pMinecraft = Minecraft::GetInstance();
            Font *font = pMinecraft->altFont;
            unsigned int col = textColour;
            if (player->experienceLevel < cost && !player->abilities.instabuild) {
                col = textDisabledColour;
            } else if (hasFocus) {
                col = textFocusColour;
            }
            if (font) {
                font->drawWordWrap(enchantmentString, 0, 0, static_cast<int>(rect.w / ss), col,
                                   static_cast<int>(rect.h / ss));
            }
            glDisable(GL_ALPHA_TEST);
        }
        glDisable(GL_RESCALE_NORMAL);
    }
};

// UIScene_EnchantingMenu, over the server's EnchantmentMenu
class EnchantingScene : public ServerContainerScene, public IUIScene_EnchantingMenu {
public:
    EnchantingScene(AbstractContainerMenu *menu, const std::wstring &name)
        : m_slotListIngredient("MainPanel.ingredient"), m_labelEnchant("MainPanel.enchantLabel"),
          m_enchantButton{EnchantmentButton("MainPanel.Button1", 0), EnchantmentButton("MainPanel.Button2", 1),
                          EnchantmentButton("MainPanel.Button3", 2)} {
        mapSharedControls();
        mapSlotList(m_slotListIngredient);
        for (EnchantmentButton &button : m_enchantButton) button.rect = rectAt(button.label.path.c_str());
        m_bookRect = rectAt("MainPanel.iggy_EnchantmentBook");

        m_labelEnchant.init(name.empty() ? std::wstring(app.GetString(IDS_ENCHANT)) : name);

        enterTutorialState(e_Tutorial_State_Enchanting_Menu);

        Initialize(0, menu, false, EnchantmentMenu::INV_SLOT_START, eSectionEnchantUsing, eSectionEnchantMax);

        m_slotListIngredient.addSlots(EnchantmentMenu::INGREDIENT_SLOT, 1);
    }

    ~EnchantingScene() override { delete m_model; }

    // UIScene::tick runs the buttons' tick, their state then their label
    void tick(double now) override {
        std::shared_ptr<ServerPlayer> player = mcle_local_player();
        if (player && getMenu()) {
            for (EnchantmentButton &button : m_enchantButton) {
                button.updateState(getMenu(), player);
                button.label.tick();
            }
        }
        ServerContainerScene::tick(now);
    }

    // UIScene_EnchantingMenu::customDraw: the book, the buttons' names, then
    // the slots
    void customDraw(const MCLECustomDrawRegion &region) override {
        int slotId = -1;
        if (strcmp(region.name, "EnchantmentBook") == 0) {
            renderBook(region);
        } else if (sscanf(region.name, "slot_Button%d", &slotId) == 1 && slotId >= 1 && slotId <= 3) {
            std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
            std::shared_ptr<ServerPlayer> player = mcle_local_player();
            if (player && getMenu()) m_enchantButton[slotId - 1].render(region, getMenu(), player);
        } else {
            ServerContainerScene::customDraw(region);
        }
    }

protected:
    MCLEUISlotList m_slotListIngredient;
    MCLEUILabel m_labelEnchant;
    EnchantmentButton m_enchantButton[3];
    MCLEControlRect m_bookRect{};

    // UIControl_EnchantmentBook
    BookModel *m_model = nullptr;
    std::shared_ptr<ItemInstance> m_last;
    Random m_random;
    int m_time = 0;
    float m_flip = 0, m_oFlip = 0, m_flipT = 0, m_flipA = 0;
    float m_open = 0, m_oOpen = 0;

    // IUIScene_EnchantingMenu::handleOtherClicked clicks the button on the
    // client's menu and handleInventoryButtonClick sends it to the server,
    // whose handleContainerButtonClick clicks it again. Here that's once,
    // on the server's menu.
    void handleOtherClicked(int iPad, ESceneSection eSection, int buttonNum, bool quickKey) override {
        int index = -1;
        switch (eSection) {
        case eSectionEnchantButton1: index = 0; break;
        case eSectionEnchantButton2: index = 1; break;
        case eSectionEnchantButton3: index = 2; break;
        default: break;
        }
        std::shared_ptr<ServerPlayer> player = mcle_local_player();
        if (index < 0 || !player) return;
        player->resetLastActionTime();
        if (player->containerMenu == m_menu && m_menu->isSynched(player)) {
            m_menu->clickMenuButton(player, index);
            m_menu->broadcastChanges();
        }
    }

    EnchantmentButton *button(ESceneSection eSection) {
        switch (eSection) {
        case eSectionEnchantButton1: return &m_enchantButton[0];
        case eSectionEnchantButton2: return &m_enchantButton[1];
        case eSectionEnchantButton3: return &m_enchantButton[2];
        default: return nullptr;
        }
    }

    // UIControl::setFocus on whichever control the section is
    void setControlFocus(ESceneSection eSection, bool focus) {
        if (EnchantmentButton *b = button(eSection)) {
            b->hasFocus = focus;
            std::shared_ptr<ServerPlayer> player = mcle_local_player();
            if (player && getMenu()) b->updateState(getMenu(), player);
        } else if (MCLEUISlotList *list = getSection(eSection)) {
            list->setFocus(focus);
        }
    }

    void setSectionFocus(ESceneSection eSection, int iPad) override {
        setControlFocus(eSection, true);
        if (m_focusSection != eSectionNone && m_focusSection != eSection) setControlFocus(m_focusSection, false);
        m_focusSection = eSection;
    }

    void setFocusToPointer(int iPad) override {
        if (m_focusSection != eSectionNone) setControlFocus(m_focusSection, false);
        m_focusSection = eSectionNone;
    }

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionEnchantSlot: cols = 1; break;
        case eSectionEnchantInventory: cols = 9; break;
        case eSectionEnchantUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionEnchantSlot: rows = 1; break;
        case eSectionEnchantInventory: rows = 3; break;
        case eSectionEnchantUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionEnchantSlot: return &m_slotListIngredient;
        case eSectionEnchantInventory: return &m_slotListInventory;
        case eSectionEnchantUsing: return &m_slotListHotbar;
        default: return nullptr;
        }
    }

    const MCLEControlRect *sectionRect(ESceneSection eSection) {
        if (EnchantmentButton *b = button(eSection)) return &b->rect;
        MCLEUISlotList *list = getSection(eSection);
        return list ? &list->rect : nullptr;
    }

    void GetPositionOfSection(ESceneSection eSection, UIVec2D *pPosition) override {
        const MCLEControlRect *rect = sectionRect(eSection);
        pPosition->x = rect ? rect->x : 0;
        pPosition->y = rect ? rect->y : 0;
    }

    void GetItemScreenData(ESceneSection eSection, int iItemIndex, UIVec2D *pPosition, UIVec2D *pSize) override {
        const MCLEControlRect *rect = sectionRect(eSection);
        UIVec2D sectionSize;
        sectionSize.x = rect ? rect->w : 0;
        sectionSize.y = rect ? rect->h : 0;

        if (IsSectionSlotList(eSection)) {
            int rows = getSectionRows(eSection);
            int cols = getSectionColumns(eSection);

            pSize->x = sectionSize.x / cols;
            pSize->y = sectionSize.y / rows;

            int itemCol = iItemIndex % cols;
            int itemRow = iItemIndex / cols;

            pPosition->x = itemCol * pSize->x;
            pPosition->y = itemRow * pSize->y;
        } else {
            GetPositionOfSection(eSection, pPosition);
            pSize->x = sectionSize.x;
            pSize->y = sectionSize.y;
        }
    }

    // UIControl_EnchantmentBook::render, the region in pixels
    void renderBook(const MCLECustomDrawRegion &region) {
        std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
        Minecraft *pMinecraft = Minecraft::GetInstance();
        EnchantmentMenu *menu = getMenu();
        if (!menu || !pMinecraft || !pMinecraft->textures || m_bookRect.w <= 0 || m_bookRect.h <= 0) return;

        glLoadIdentity();
        glTranslatef(region.x0, region.y0, -2000);
        glEnable(GL_RESCALE_NORMAL);
        glPushMatrix();

        float width = region.x1 - region.x0;
        float height = region.y1 - region.y0;

        // Revert the scale from the setup
        float ssX = width / m_bookRect.w;
        float ssY = height / m_bookRect.h;
        glScalef(ssX, ssY, 1.0f);

        glTranslatef(m_bookRect.w / 2, m_bookRect.h / 2, 50.0f);

        // Add a uniform scale
        glScalef(-57 / ssX, 57 / ssX, 360.0f);

        glRotatef(45 + 90, 0, 1, 0);
        Lighting::turnOn();
        glRotatef(-45 - 90, 0, 1, 0);

        int tex = pMinecraft->textures->loadTexture(TN_ITEM_BOOK);
        pMinecraft->textures->bind(tex);
        glRotatef(20, 1, 0, 0);

        float a = 1;
        float o = m_oOpen + (m_open - m_oOpen) * a;
        glTranslatef((1 - o) * 0.2f, (1 - o) * 0.1f, (1 - o) * 0.25f);
        glRotatef(-(1 - o) * 90 - 90, 0, 1, 0);
        glRotatef(180, 1, 0, 0);

        float ff1 = m_oFlip + (m_flip - m_oFlip) * a + 0.25f;
        float ff2 = m_oFlip + (m_flip - m_oFlip) * a + 0.75f;
        ff1 = (ff1 - floor(ff1)) * 1.6f - 0.3f;
        ff2 = (ff2 - floor(ff2)) * 1.6f - 0.3f;

        if (ff1 < 0) ff1 = 0;
        if (ff2 < 0) ff2 = 0;
        if (ff1 > 1) ff1 = 1;
        if (ff2 > 1) ff2 = 1;

        glEnable(GL_CULL_FACE);
        // Upstream shares the EnchantTableRenderer's model, which only
        // UIControl_EnchantmentBook is a friend of, so this is its fallback
        if (m_model == nullptr) m_model = new BookModel();
        m_model->render(nullptr, 0, ff1, ff2, o, 0, 1 / 16.0f, true);
        glDisable(GL_CULL_FACE);

        glPopMatrix();
        Lighting::turnOff();
        glDisable(GL_RESCALE_NORMAL);

        tickBook(menu);
    }

    // UIControl_EnchantmentBook::tickBook, once a draw like upstream
    void tickBook(EnchantmentMenu *menu) {
        shared_ptr<ItemInstance> current = menu->getSlot(0)->getItem();
        if (!ItemInstance::matches(current, m_last)) {
            m_last = current;
            do {
                m_flipT += m_random.nextInt(4) - m_random.nextInt(4);
            } while (m_flip <= m_flipT + 1 && m_flip >= m_flipT - 1);
        }

        m_time++;
        m_oFlip = m_flip;
        m_oOpen = m_open;

        bool shouldBeOpen = false;
        for (int i = 0; i < 3; i++) {
            if (menu->costs[i] != 0) shouldBeOpen = true;
        }

        if (shouldBeOpen) m_open += 0.2f;
        else m_open -= 0.2f;
        if (m_open < 0) m_open = 0;
        if (m_open > 1) m_open = 1;

        float diff = (m_flipT - m_flip) * 0.4f;
        float max = 0.2f;
        if (diff < -max) diff = -max;
        if (diff > +max) diff = +max;
        m_flipA += (diff - m_flipA) * 0.9f;
        m_flip = m_flip + m_flipA;
    }
};

// UIControl_BeaconEffectButton
struct BeaconEffectButton {
    enum EState { eState_Disabled, eState_Enabled_Unselected, eState_Enabled_Selected, eState_Pressed };

    std::string path;
    MCLEControlRect rect{};
    int data = 0;
    int icon = 0;
    bool selected = false;
    bool active = false;
    bool focus = false;
    bool visible = true;
    int lastState = -1;

    explicit BeaconEffectButton(const char *p) : path(p) {}

    void setVisible(bool show) {
        if (show != visible) {
            mcle_ui_set_visible(path, show);
            visible = show;
        }
    }

    void SetData(int newData, int newIcon, bool newActive, bool newSelected) {
        data = newData;
        active = newActive;
        selected = newSelected;
        SetIcon(newIcon);
        UpdateButtonState();
    }

    void SetButtonSelected(bool newSelected) {
        if (newSelected != selected) {
            selected = newSelected;
            UpdateButtonState();
        }
    }

    void SetButtonActive(bool newActive) {
        if (newActive != active) {
            active = newActive;
            UpdateButtonState();
        }
    }

    void setFocus(bool newFocus) {
        if (newFocus != focus) {
            focus = newFocus;
            UpdateButtonState();
        }
    }

    void SetIcon(int newIcon) {
        if (newIcon != icon) {
            icon = newIcon;
            mcle_ui_call(path, "SetIcon", {mcle_ui_num(icon)});
        }
    }

    void UpdateButtonState() {
        EState state;
        if (!active) state = eState_Disabled;
        else if (selected) state = eState_Pressed;
        else if (focus) state = eState_Enabled_Selected;
        else state = eState_Enabled_Unselected;

        if (state != lastState) {
            mcle_ui_call(path, "ChangeState", {mcle_ui_num(state)});
            lastState = state;
        }
    }
};

// UIScene_BeaconMenu. m_beacon is the client's own beacon, which takes the
// server's levels and powers when the menu opens and its payment slot as
// it changes; the powers picked here only reach the server's beacon with
// the confirm, as on console.
class BeaconScene : public ServerContainerScene, public IUIScene_BeaconMenu {
public:
    enum EControls {
        eControl_Primary1,
        eControl_Primary2,
        eControl_Primary3,
        eControl_Primary4,
        eControl_Primary5,
        eControl_Secondary1,
        eControl_Secondary2,
        eControl_EFFECT_COUNT,
    };

    BeaconScene(AbstractContainerMenu *menu, const std::shared_ptr<BeaconTileEntity> &serverBeacon)
        : m_serverBeacon(serverBeacon), m_slotListActivator("MainPanel.ActivatorSlot"),
          m_slotListActivatorIcons("MainPanel.ActivatorList"), m_labelPrimary("MainPanel.PrimaryPowerLabel"),
          m_labelSecondary("MainPanel.SecondaryPowerLabel"),
          m_buttonsPowers{BeaconEffectButton("MainPanel.Primary_Slot_01"), BeaconEffectButton("MainPanel.Primary_Slot_02"),
                          BeaconEffectButton("MainPanel.Primary_Slot_03"), BeaconEffectButton("MainPanel.Primary_Slot_04"),
                          BeaconEffectButton("MainPanel.Primary_Slot_05"), BeaconEffectButton("MainPanel.Secondary_Slot_01"),
                          BeaconEffectButton("MainPanel.Secondary_Slot_02")},
          m_buttonConfirm("MainPanel.ConfirmButton") {
        mapSharedControls();
        mapSlotList(m_slotListActivator);
        mapSlotList(m_slotListActivatorIcons);
        for (BeaconEffectButton &button : m_buttonsPowers) button.rect = rectAt(button.path.c_str());
        m_buttonConfirm.rect = rectAt(m_buttonConfirm.path.c_str());

        m_labelPrimary.init(app.GetString(IDS_CONTAINER_BEACON_PRIMARY_POWER));
        m_labelSecondary.init(app.GetString(IDS_CONTAINER_BEACON_SECONDARY_POWER));

        for (BeaconEffectButton &button : m_buttonsPowers) button.setVisible(false);

        enterTutorialState(e_Tutorial_State_Beacon_Menu);

        // BeaconMenu::addSlotListener's setContainerData, then its slot
        m_beacon = std::make_shared<BeaconTileEntity>();
        m_beacon->setLevels(m_serverBeacon->getLevels());
        m_beacon->setPrimaryPower(m_serverBeacon->getPrimaryPower());
        m_beacon->setSecondaryPower(m_serverBeacon->getSecondaryPower());
        m_beacon->setItem(0, m_serverBeacon->getItem(0));

        Initialize(0, menu, false, BeaconMenu::INV_SLOT_START, eSectionBeaconUsing, eSectionBeaconMax);

        m_slotListActivator.addSlots(BeaconMenu::PAYMENT_SLOT, 1);
        m_slotListActivatorIcons.addSlots(m_menu->getSize(), 4);
    }

    void tick(double now) override {
        ServerContainerScene::tick(now);
        // the server's ContainerSetSlot for the payment
        m_beacon->setItem(0, m_serverBeacon->getItem(0));
        handleTick();
    }

    // The activator list's four item icons, then the slots
    void customDraw(const MCLECustomDrawRegion &region) override {
        int slotId = -1;
        if (sscanf(region.name, "slot_%d", &slotId) == 1 && slotId >= 0 &&
            slotId >= static_cast<int>(m_slotItems.size())) {
            std::shared_ptr<ItemInstance> item = nullptr;
            switch (slotId - static_cast<int>(m_slotItems.size())) {
            case 0: item = std::make_shared<ItemInstance>(Item::emerald); break;
            case 1: item = std::make_shared<ItemInstance>(Item::diamond); break;
            case 2: item = std::make_shared<ItemInstance>(Item::goldIngot); break;
            case 3: item = std::make_shared<ItemInstance>(Item::ironIngot); break;
            default: break;
            }
            if (item != nullptr) mcle_custom_draw_slot(region, item, 1.0f, true);
        } else {
            ServerContainerScene::customDraw(region);
        }
    }

protected:
    std::shared_ptr<BeaconTileEntity> m_serverBeacon;
    MCLEUISlotList m_slotListActivator, m_slotListActivatorIcons;
    MCLEUILabel m_labelPrimary, m_labelSecondary;
    BeaconEffectButton m_buttonsPowers[eControl_EFFECT_COUNT];
    BeaconEffectButton m_buttonConfirm;

    BeaconEffectButton *button(ESceneSection eSection) {
        switch (eSection) {
        case eSectionBeaconPrimaryTierOneOne: return &m_buttonsPowers[eControl_Primary1];
        case eSectionBeaconPrimaryTierOneTwo: return &m_buttonsPowers[eControl_Primary2];
        case eSectionBeaconPrimaryTierTwoOne: return &m_buttonsPowers[eControl_Primary3];
        case eSectionBeaconPrimaryTierTwoTwo: return &m_buttonsPowers[eControl_Primary4];
        case eSectionBeaconPrimaryTierThree: return &m_buttonsPowers[eControl_Primary5];
        case eSectionBeaconSecondaryOne: return &m_buttonsPowers[eControl_Secondary1];
        case eSectionBeaconSecondaryTwo: return &m_buttonsPowers[eControl_Secondary2];
        case eSectionBeaconConfirm: return &m_buttonConfirm;
        default: return nullptr;
        }
    }

    // UIControl::setFocus on whichever control the section is
    void setControlFocus(ESceneSection eSection, bool focus) {
        if (BeaconEffectButton *b = button(eSection)) b->setFocus(focus);
        else if (MCLEUISlotList *list = getSection(eSection)) list->setFocus(focus);
    }

    void setSectionFocus(ESceneSection eSection, int iPad) override {
        setControlFocus(eSection, true);
        if (m_focusSection != eSectionNone && m_focusSection != eSection) setControlFocus(m_focusSection, false);
        m_focusSection = eSection;
    }

    void setFocusToPointer(int iPad) override {
        if (m_focusSection != eSectionNone) setControlFocus(m_focusSection, false);
        m_focusSection = eSectionNone;
    }

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionBeaconItem: cols = 1; break;
        case eSectionBeaconInventory: cols = 9; break;
        case eSectionBeaconUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionBeaconItem: rows = 1; break;
        case eSectionBeaconInventory: rows = 3; break;
        case eSectionBeaconUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionBeaconItem: return &m_slotListActivator;
        case eSectionBeaconInventory: return &m_slotListInventory;
        case eSectionBeaconUsing: return &m_slotListHotbar;
        default: return nullptr;
        }
    }

    const MCLEControlRect *sectionRect(ESceneSection eSection) {
        if (BeaconEffectButton *b = button(eSection)) return &b->rect;
        MCLEUISlotList *list = getSection(eSection);
        return list ? &list->rect : nullptr;
    }

    void GetPositionOfSection(ESceneSection eSection, UIVec2D *pPosition) override {
        const MCLEControlRect *rect = sectionRect(eSection);
        pPosition->x = rect ? rect->x : 0;
        pPosition->y = rect ? rect->y : 0;
    }

    void GetItemScreenData(ESceneSection eSection, int iItemIndex, UIVec2D *pPosition, UIVec2D *pSize) override {
        const MCLEControlRect *rect = sectionRect(eSection);
        UIVec2D sectionSize;
        sectionSize.x = rect ? rect->w : 0;
        sectionSize.y = rect ? rect->h : 0;

        if (IsSectionSlotList(eSection)) {
            int rows = getSectionRows(eSection);
            int cols = getSectionColumns(eSection);

            pSize->x = sectionSize.x / cols;
            pSize->y = sectionSize.y / rows;

            int itemCol = iItemIndex % cols;
            int itemRow = iItemIndex / cols;

            pPosition->x = itemCol * pSize->x;
            pPosition->y = itemRow * pSize->y;
        } else {
            GetPositionOfSection(eSection, pPosition);
            pSize->x = sectionSize.x;
            pSize->y = sectionSize.y;
        }
    }

    void SetConfirmButtonEnabled(bool enabled) override { m_buttonConfirm.SetButtonActive(enabled); }

    void AddPowerButton(int id, int icon, int tier, int count, bool active, bool selected) override {
        BeaconEffectButton *target = nullptr;
        switch (tier) {
        case 0: target = &m_buttonsPowers[count == 0 ? eControl_Primary1 : eControl_Primary2]; break;
        case 1: target = &m_buttonsPowers[count == 0 ? eControl_Primary3 : eControl_Primary4]; break;
        case 2: target = &m_buttonsPowers[eControl_Primary5]; break;
        case 3: target = &m_buttonsPowers[count == 0 ? eControl_Secondary1 : eControl_Secondary2]; break;
        }
        if (!target) return;
        target->SetData(id, icon, active, selected);
        target->setVisible(true);
    }

    int GetPowerButtonId(ESceneSection eSection) override {
        BeaconEffectButton *b = button(eSection);
        return (b && b != &m_buttonConfirm) ? b->data : 0;
    }

    bool IsPowerButtonSelected(ESceneSection eSection) override {
        BeaconEffectButton *b = button(eSection);
        return (b && b != &m_buttonConfirm) ? b->selected : false;
    }

    void SetPowerButtonSelected(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionBeaconPrimaryTierOneOne:
        case eSectionBeaconPrimaryTierOneTwo:
        case eSectionBeaconPrimaryTierTwoOne:
        case eSectionBeaconPrimaryTierTwoTwo:
        case eSectionBeaconPrimaryTierThree:
            for (int i = eControl_Primary1; i <= eControl_Primary5; ++i) m_buttonsPowers[i].SetButtonSelected(false);
            break;
        case eSectionBeaconSecondaryOne:
        case eSectionBeaconSecondaryTwo:
            m_buttonsPowers[eControl_Secondary1].SetButtonSelected(false);
            m_buttonsPowers[eControl_Secondary2].SetButtonSelected(false);
            break;
        default:
            break;
        }
        BeaconEffectButton *b = button(eSection);
        if (b && b != &m_buttonConfirm) b->SetButtonSelected(true);
    }
};

// UIControl_TextInput: Init with its label and id, the label like a
// label's, and ChangeState for the focus
struct AnvilTextInput : MCLEUILabel {
    MCLEControlRect rect{};
    bool hasFocus = false;

    explicit AnvilTextInput(const char *p) : MCLEUILabel(p) {}

    void init(const std::wstring &text, int id) {
        label = text;
        mcle_ui_call(path, "Init", {mcle_ui_str(text), mcle_ui_num(id)});
    }

    void setFocus(bool focus) {
        if (hasFocus == focus) return;
        hasFocus = focus;
        mcle_ui_call(path, "ChangeState", {mcle_ui_num(focus ? 0 : 1)});
    }
};

// UIScene_AnvilMenu. m_repairMenu is the server's AnvilMenu, so the cost is
// its own and the scene listens to its slots. The server's broadcastChanges
// can call slotChanged from the sim thread, which only sets labels for the
// next tick.
class AnvilScene : public ServerContainerScene, public IUIScene_AnvilMenu {
public:
    enum EControls {
        eControl_TextInput,
    };

    AnvilScene(AnvilMenu *menu, const std::shared_ptr<Inventory> &inventory)
        : m_slotListItem1("MainPanel.Ingredient"), m_slotListItem2("MainPanel.Ingredient2"),
          m_slotListResult("MainPanel.Result"), m_labelAnvil("MainPanel.AnvilText"),
          m_textInputAnvil("MainPanel.AnvilTextInput") {
        mapSharedControls();
        mapSlotList(m_slotListItem1);
        mapSlotList(m_slotListItem2);
        mapSlotList(m_slotListResult);
        m_textInputAnvil.rect = rectAt(m_textInputAnvil.path.c_str());

        m_showingCross = false;
        m_textInputAnvil.init(m_itemName, eControl_TextInput);

        m_labelAnvil.init(app.GetString(IDS_REPAIR_AND_NAME));

        m_inventory = inventory;

        enterTutorialState(e_Tutorial_State_Anvil_Menu);

        m_repairMenu = menu;
        m_repairMenu->addSlotListener(this);

        Initialize(0, m_repairMenu, false, AnvilMenu::INV_SLOT_START, eSectionAnvilUsing, eSectionAnvilMax);

        m_slotListItem1.addSlots(AnvilMenu::INPUT_SLOT, 1);
        m_slotListItem2.addSlots(AnvilMenu::ADDITIONAL_SLOT, 1);
        m_slotListResult.addSlots(AnvilMenu::RESULT_SLOT, 1);

        bool expensive = false;
        wstring m_costString = L"";

        if (m_repairMenu->cost > 0) {
            if (m_repairMenu->cost >= 40 && !mcle_container_player()->abilities.instabuild) {
                m_costString = app.GetString(IDS_REPAIR_EXPENSIVE);
                expensive = true;
            } else if (!m_repairMenu->getSlot(AnvilMenu::RESULT_SLOT)->hasItem()) {
                // Do nothing
            } else {
                LPCWSTR costString = app.GetString(IDS_REPAIR_COST);
                wchar_t temp[256];
                swprintf(temp, 256, costString, m_repairMenu->cost);
                m_costString = temp;
                if (!m_repairMenu->getSlot(AnvilMenu::RESULT_SLOT)
                         ->mayPickup(dynamic_pointer_cast<Player>(m_inventory->player->shared_from_this()))) {
                    expensive = true;
                }
            }
        }
        setCostLabel(m_costString, expensive);

        setIgnoreInput(false);
    }

    void tick(double now) override {
        m_labelAnvil.tick();
        m_textInputAnvil.tick();
        ServerContainerScene::tick(now);
        handleTick();
    }

    // KeyboardCompleteCallback
    void keyboardComplete(bool ok, const std::wstring &text) override {
        setIgnoreInput(false);
        if (ok) {
            setEditNameValue(text);
            m_itemName = text;
            updateItemName();
        }
    }

    // The menu outlives the scene here, so the scene stops listening first
    void handleDestroy() override {
        m_repairMenu->removeSlotListener(this);
        ServerContainerScene::handleDestroy();
    }

protected:
    bool m_showingCross = false;
    MCLEUISlotList m_slotListItem1, m_slotListItem2, m_slotListResult;
    MCLEUILabel m_labelAnvil;
    AnvilTextInput m_textInputAnvil;
    std::wstring m_lastCostLabel;
    bool m_lastCanAfford = false;
    bool m_costSent = false;

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionAnvilItem1: cols = 1; break;
        case eSectionAnvilItem2: cols = 1; break;
        case eSectionAnvilResult: cols = 1; break;
        case eSectionAnvilInventory: cols = 9; break;
        case eSectionAnvilUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionAnvilItem1: rows = 1; break;
        case eSectionAnvilItem2: rows = 1; break;
        case eSectionAnvilResult: rows = 1; break;
        case eSectionAnvilInventory: rows = 3; break;
        case eSectionAnvilUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionAnvilItem1: return &m_slotListItem1;
        case eSectionAnvilItem2: return &m_slotListItem2;
        case eSectionAnvilResult: return &m_slotListResult;
        case eSectionAnvilInventory: return &m_slotListInventory;
        case eSectionAnvilUsing: return &m_slotListHotbar;
        default: return nullptr;
        }
    }

    const MCLEControlRect *sectionRect(ESceneSection eSection) {
        if (eSection == eSectionAnvilName) return &m_textInputAnvil.rect;
        MCLEUISlotList *list = getSection(eSection);
        return list ? &list->rect : nullptr;
    }

    void GetPositionOfSection(ESceneSection eSection, UIVec2D *pPosition) override {
        const MCLEControlRect *rect = sectionRect(eSection);
        pPosition->x = rect ? rect->x : 0;
        pPosition->y = rect ? rect->y : 0;
    }

    void GetItemScreenData(ESceneSection eSection, int iItemIndex, UIVec2D *pPosition, UIVec2D *pSize) override {
        const MCLEControlRect *rect = sectionRect(eSection);
        UIVec2D sectionSize;
        sectionSize.x = rect ? rect->w : 0;
        sectionSize.y = rect ? rect->h : 0;

        if (IsSectionSlotList(eSection)) {
            int rows = getSectionRows(eSection);
            int cols = getSectionColumns(eSection);

            pSize->x = sectionSize.x / cols;
            pSize->y = sectionSize.y / rows;

            int itemCol = iItemIndex % cols;
            int itemRow = iItemIndex / cols;

            pPosition->x = itemCol * pSize->x;
            pPosition->y = itemRow * pSize->y;
        } else {
            GetPositionOfSection(eSection, pPosition);
            pSize->x = sectionSize.x;
            pSize->y = sectionSize.y;
        }
    }

    // UIControl::setFocus on the name box or the slot list
    void setControlFocus(ESceneSection eSection, bool focus) {
        if (eSection == eSectionAnvilName) m_textInputAnvil.setFocus(focus);
        else if (MCLEUISlotList *list = getSection(eSection)) list->setFocus(focus);
    }

    void setSectionFocus(ESceneSection eSection, int iPad) override {
        setControlFocus(eSection, true);
        if (m_focusSection != eSectionNone && m_focusSection != eSection) setControlFocus(m_focusSection, false);
        m_focusSection = eSection;
    }

    void setFocusToPointer(int iPad) override {
        if (m_focusSection != eSectionNone) setControlFocus(m_focusSection, false);
        m_focusSection = eSectionNone;
    }

    void handleEditNamePressed() override {
        setIgnoreInput(true);
        mcle_ui_request_keyboard(app.GetString(IDS_TITLE_RENAME), m_textInputAnvil.label, 30);
    }

    void setEditNameValue(const wstring &name) override { m_textInputAnvil.setLabel(name); }

    void setEditNameEditable(bool enabled) override {}

    // handleTick sets it every tick; the movie keeps the last one, so it's
    // sent when it changes
    void setCostLabel(const wstring &label, bool canAfford) override {
        if (m_costSent && label == m_lastCostLabel && canAfford == m_lastCanAfford) return;
        m_costSent = true;
        m_lastCostLabel = label;
        m_lastCanAfford = canAfford;
        mcle_ui_call("", "SetCostLabel", {mcle_ui_str(label), mcle_ui_bool(canAfford)});
    }

    void showCross(bool show) override {
        if (m_showingCross != show) {
            mcle_ui_call("", "ShowRedCross", {mcle_ui_bool(show)});
            m_showingCross = show;
        }
    }
};

// UIScene_FireworksMenu, over the server's FireworksMenu
class FireworksScene : public ServerContainerScene, public IUIScene_FireworksMenu {
public:
    explicit FireworksScene(AbstractContainerMenu *menu)
        : m_slotListResult("MainPanel.Result"), m_slotList3x3("MainPanel.Fireworks3x3"),
          m_slotList2x2("MainPanel.Fireworks2x2"), m_labelFireworks("MainPanel.FireworksLabel") {
        mapSharedControls();
        mapSlotList(m_slotListResult);
        mapSlotList(m_slotList3x3);
        mapSlotList(m_slotList2x2);

        m_labelFireworks.init(app.GetString(IDS_HOW_TO_PLAY_MENU_FIREWORKS));

        enterTutorialState(e_Tutorial_State_Fireworks_Menu);

        Initialize(0, menu, false, FireworksMenu::INV_SLOT_START, eSectionFireworksUsing, eSectionFireworksMax);

        m_slotListResult.addSlots(FireworksMenu::RESULT_SLOT, 1);
        m_slotList3x3.addSlots(FireworksMenu::CRAFT_SLOT_START, 9);
        ShowLargeCraftingGrid(true);
    }

protected:
    MCLEUISlotList m_slotListResult, m_slotList3x3, m_slotList2x2;
    MCLEUILabel m_labelFireworks;

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionFireworksIngredients: cols = 3; break;
        case eSectionFireworksResult: cols = 1; break;
        case eSectionFireworksInventory: cols = 9; break;
        case eSectionFireworksUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionFireworksIngredients: rows = 3; break;
        case eSectionFireworksResult: rows = 1; break;
        case eSectionFireworksInventory: rows = 3; break;
        case eSectionFireworksUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionFireworksIngredients: return &m_slotList3x3;
        case eSectionFireworksResult: return &m_slotListResult;
        case eSectionFireworksInventory: return &m_slotListInventory;
        case eSectionFireworksUsing: return &m_slotListHotbar;
        default: assert(false); return nullptr;
        }
    }

    // bShow == true removes the 2x2 crafting grid and bShow == false removes the 3x3 crafting grid
    void ShowLargeCraftingGrid(bool bShow) { mcle_ui_call("", "ShowLargeCraftingGrid", {mcle_ui_bool(bShow)}); }
};

// UIScene_HorseInventoryMenu. m_horse is the client's copy of the horse and
// m_container the AnimalChest it makes for the packet; here they're the
// server's horse and the inventory the menu is over.
class HorseScene : public ServerContainerScene, public IUIScene_HorseInventoryMenu {
public:
    HorseScene(AbstractContainerMenu *menu, const std::shared_ptr<Inventory> &inventory,
               const std::shared_ptr<Container> &container, const std::shared_ptr<EntityHorse> &horse,
               const std::wstring &name)
        : m_slotSaddle("MainPanel.SlotSaddle"), m_slotArmor("MainPanel.SlotArmor"),
          m_slotListChest("MainPanel.DonkeyInventoryList"), m_labelHorse("MainPanel.horseinventoryText") {
        mapSharedControls();
        mapSlotList(m_slotSaddle);
        mapSlotList(m_slotArmor);
        mapSlotList(m_slotListChest);
        m_horseRect = rectAt("MainPanel.iggy_horse");

        m_labelHorse.init(name);
        m_inventory = inventory;
        m_container = container;
        m_horse = horse;

        enterTutorialState(e_Tutorial_State_Horse_Menu);

        int startSlot = EntityHorse::INV_BASE_COUNT;
        if (m_horse->isChestedHorse()) {
            startSlot += EntityHorse::INV_DONKEY_CHEST_COUNT;
        }
        Initialize(0, menu, false, startSlot, eSectionHorseUsing, eSectionHorseMax);

        m_slotSaddle.addSlots(EntityHorse::INV_SLOT_SADDLE, 1);
        m_slotArmor.addSlots(EntityHorse::INV_SLOT_ARMOR, 1);

        if (m_horse->isChestedHorse()) {
            // also starts at one, because a donkey can't wear armor!
            m_slotListChest.addSlots(EntityHorse::INV_BASE_COUNT, EntityHorse::INV_DONKEY_CHEST_COUNT);
        }

        // remove horse inventory
        if (!m_horse->isChestedHorse()) SetHasInventory(false);

        // cannot wear armor? remove armor slot!
        if (!m_horse->canWearArmor()) SetIsDonkey(true);

        setIgnoreInput(false);
    }

    // UIScene_HorseInventoryMenu::customDraw: the horse, then the slots
    void customDraw(const MCLECustomDrawRegion &region) override {
        if (strcmp(region.name, "horse") == 0) {
            renderHorse(region);
            return;
        }
        ServerContainerScene::customDraw(region);
    }

protected:
    MCLEUISlotList m_slotSaddle, m_slotArmor, m_slotListChest;
    MCLEUILabel m_labelHorse;
    // The iggy_horse control, UIControl_MinecraftHorse, MainPanel coordinates
    MCLEControlRect m_horseRect{};

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionHorseArmor: cols = 1; break;
        case eSectionHorseSaddle: cols = 1; break;
        case eSectionHorseChest: cols = 5; break;
        case eSectionHorseInventory: cols = 9; break;
        case eSectionHorseUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionHorseArmor: rows = 1; break;
        case eSectionHorseSaddle: rows = 1; break;
        case eSectionHorseChest: rows = 3; break;
        case eSectionHorseInventory: rows = 3; break;
        case eSectionHorseUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionHorseArmor: return &m_slotArmor;
        case eSectionHorseSaddle: return &m_slotSaddle;
        case eSectionHorseChest: return &m_slotListChest;
        case eSectionHorseInventory: return &m_slotListInventory;
        case eSectionHorseUsing: return &m_slotListHotbar;
        default: assert(false); return nullptr;
        }
    }

    void GetItemScreenData(ESceneSection eSection, int iItemIndex, UIVec2D *pPosition, UIVec2D *pSize) override {
        MCLEUISlotList *list = getSection(eSection);
        UIVec2D sectionSize;
        sectionSize.x = list ? list->rect.w : 0;
        sectionSize.y = list ? list->rect.h : 0;

        if (IsSectionSlotList(eSection)) {
            int rows = getSectionRows(eSection);
            int cols = getSectionColumns(eSection);

            pSize->x = sectionSize.x / cols;
            pSize->y = sectionSize.y / rows;

            int itemCol = iItemIndex % cols;
            int itemRow = iItemIndex / cols;

            pPosition->x = itemCol * pSize->x;
            pPosition->y = itemRow * pSize->y;
        } else {
            GetPositionOfSection(eSection, pPosition);
            pSize->x = sectionSize.x;
            pSize->y = sectionSize.y;
        }
    }

    void SetHasInventory(bool bHasInventory) { mcle_ui_call("", "SetHasInventory", {mcle_ui_bool(bHasInventory)}); }

    void SetIsDonkey(bool bSetIsDonkey) { mcle_ui_call("", "SetIsDonkey", {mcle_ui_bool(bSetIsDonkey)}); }

    // UIControl_MinecraftHorse::render, the horse turned towards the pointer.
    // The region is in pixels already, so it's translated to rather than
    // scaled from Iggy's double size target. m_fScreenWidth /
    // m_fScreenHeight is the 16:9 screen the movie is laid out for.
    void renderHorse(const MCLECustomDrawRegion &region) {
        std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
        Minecraft *pMinecraft = Minecraft::GetInstance();
        if (!m_horse || !EntityRenderDispatcher::instance || !pMinecraft || !pMinecraft->options) return;
        float pointerX = m_pointerPos.x, pointerY = m_pointerPos.y;

        // ui.setupCustomDraw
        mcle_custom_draw_setup_state();
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
        glTranslatef(xo, yo - (height / 7.5f), 50.0f);

        shared_ptr<LivingEntity> entityHorse = m_horse;

        // Base scale on height of this control
        // Potentially we might want separate x & y scales here
        float ss = width / (1920.0f / 1080.0f) * 0.71f;

        glScalef(-ss, ss, ss);
        glRotatef(180, 0, 0, 1);

        float oybr = entityHorse->yBodyRot;
        float oyr = entityHorse->yRot;
        float oxr = entityHorse->xRot;
        float oyhr = entityHorse->yHeadRot;

        float xd = (m_horseRect.x + m_horseRect.w / 2) - pointerX;

        // Need to base Y on head position, not centre of mass
        float yd = (m_horseRect.y + m_horseRect.h / 2 - 40) - pointerY;

        glRotatef(45 + 90, 0, 1, 0);
        Lighting::turnOn();
        glRotatef(-45 - 90, 0, 1, 0);

        glRotatef(-(float)atan(yd / 40.0f) * 20, 1, 0, 0);

        entityHorse->yBodyRot = (float)atan(xd / 40.0f) * 20;
        entityHorse->yRot = (float)atan(xd / 40.0f) * 40;
        entityHorse->xRot = -(float)atan(yd / 40.0f) * 20;
        entityHorse->yHeadRot = entityHorse->yRot;
        glTranslatef(0, entityHorse->heightOffset, 0);
        EntityRenderDispatcher::instance->playerRotY = 180;

        // 4J Stu - Turning on hideGui while we do this stops the name rendering in split-screen
        bool wasHidingGui = pMinecraft->options->hideGui;
        pMinecraft->options->hideGui = true;
        try {
            EntityRenderDispatcher::instance->render(entityHorse, 0, 0, 0, 0, 1, false, false);
        } catch (...) {
        }
        pMinecraft->options->hideGui = wasHidingGui;

        entityHorse->yBodyRot = oybr;
        entityHorse->yRot = oyr;
        entityHorse->xRot = oxr;
        entityHorse->yHeadRot = oyhr;
        glPopMatrix();
        Lighting::turnOff();
        glDisable(GL_RESCALE_NORMAL);
    }
};

// The client's SimpleContainer for the chest packet types, and its getName
std::wstring chestName(const OpenPacket &packet) {
    int chestString;
    switch (packet.type) {
    case ContainerOpenPacket::MINECART_CHEST: chestString = IDS_ITEM_MINECART; break;
    case ContainerOpenPacket::BONUS_CHEST: chestString = IDS_BONUS_CHEST; break;
    case ContainerOpenPacket::LARGE_CHEST: chestString = IDS_CHEST_LARGE; break;
    case ContainerOpenPacket::ENDER_CHEST: chestString = IDS_TILE_ENDERCHEST; break;
    default: chestString = IDS_CHEST; break;
    }
    return packet.title.empty() ? std::wstring(app.GetString(chestString)) : packet.title;
}

// The client's AnimalChest for the horse packet, and its getName
std::wstring horseName(const OpenPacket &packet, const std::shared_ptr<EntityHorse> &horse) {
    int iTitle = IDS_CONTAINER_ANIMAL;
    switch (horse->getType()) {
    case EntityHorse::TYPE_DONKEY: iTitle = IDS_DONKEY; break;
    case EntityHorse::TYPE_MULE: iTitle = IDS_MULE; break;
    }
    return packet.title.empty() ? std::wstring(app.GetString(iTitle)) : packet.title;
}

// The container the menu's slots are over, the one the client's copy mirrors
std::shared_ptr<Container> menuContainer(AbstractContainerMenu *menu) {
    Slot *slot = menu->getSlot(0);
    return slot ? slot->container : nullptr;
}

} // namespace

void mcle_container_set_rect_reader(MCLEControlRect (*reader)(const char *path)) {
    g_rectReader = reader;
}

bool mcle_container_take_server_packet(int type, int containerId, const std::wstring &title, bool customName,
                                       int size, int entityId) {
    if (!hasScene(type)) return false;
    g_packet.type = type;
    g_packet.containerId = containerId;
    g_packet.title = title;
    g_packet.customName = customName;
    g_packet.size = size;
    g_packet.entityId = entityId;
    return true;
}

// The scene's getMoviePath. CMinecraftApp::LoadContainerMenu picks the large
// chest past three rows.
const char *mcle_container_server_movie() {
    if (isChest(g_packet.type)) return g_packet.size > 3 * 9 ? "ChestLargeMenu1080.swf" : "ChestMenu1080.swf";
    switch (g_packet.type) {
    case ContainerOpenPacket::FURNACE: return "FurnaceMenu1080.swf";
    case ContainerOpenPacket::TRAP:
    case ContainerOpenPacket::DROPPER: return "DispenserMenu1080.swf";
    case ContainerOpenPacket::HOPPER: return "HopperMenu1080.swf";
    case ContainerOpenPacket::BREWING_STAND: return "BrewingStandMenu1080.swf";
    case ContainerOpenPacket::ENCHANTMENT: return "EnchantingMenu1080.swf";
    case ContainerOpenPacket::BEACON: return "BeaconMenu1080.swf";
    case ContainerOpenPacket::REPAIR_TABLE: return "AnvilMenu1080.swf";
    case ContainerOpenPacket::TRADER_NPC: return "TradingMenu1080.swf";
    case ContainerOpenPacket::HORSE: return "HorseInventoryMenu1080.swf";
    case ContainerOpenPacket::FIREWORKS: return "FireworksMenu1080.swf";
    }
    return "";
}

// ClientConnection::handleContainerOpen's case for the packet's type, then
// LocalPlayer's open plays the press
bool mcle_container_open_server() {
    if (mcle_container_is_open()) return false;
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player) return false;
    AbstractContainerMenu *menu = player->containerMenu;
    if (!menu || menu == player->inventoryMenu || menu->containerId != g_packet.containerId) return false;
    std::shared_ptr<Container> container = menuContainer(menu);
    if (!container) return false;

    mcle_container_prepare();
    mcle_game_set_menu_displayed(true);
    MCLEContainerScene *scene = nullptr;
    if (isChest(g_packet.type)) {
        if (dynamic_cast<ContainerMenu *>(menu)) scene = new ChestScene(menu, chestName(g_packet));
    } else {
        switch (g_packet.type) {
        case ContainerOpenPacket::FURNACE: {
            std::shared_ptr<FurnaceTileEntity> furnace = std::dynamic_pointer_cast<FurnaceTileEntity>(container);
            if (dynamic_cast<FurnaceMenu *>(menu) && furnace) scene = new FurnaceScene(menu, furnace);
        } break;
        case ContainerOpenPacket::TRAP:
        case ContainerOpenPacket::DROPPER:
            if (dynamic_cast<TrapMenu *>(menu)) scene = new TrapScene(menu, container);
            break;
        case ContainerOpenPacket::HOPPER:
            if (dynamic_cast<HopperMenu *>(menu)) scene = new HopperScene(menu, container);
            break;
        case ContainerOpenPacket::BREWING_STAND: {
            std::shared_ptr<BrewingStandTileEntity> brewingStand =
                std::dynamic_pointer_cast<BrewingStandTileEntity>(container);
            if (dynamic_cast<BrewingStandMenu *>(menu) && brewingStand) scene = new BrewingScene(menu, brewingStand);
        } break;
        case ContainerOpenPacket::ENCHANTMENT:
            // LocalPlayer::startEnchanting gets the name only when it's custom
            if (dynamic_cast<EnchantmentMenu *>(menu)) {
                scene = new EnchantingScene(menu, g_packet.customName ? g_packet.title : std::wstring());
            }
            break;
        case ContainerOpenPacket::BEACON:
            if (BeaconMenu *beaconMenu = dynamic_cast<BeaconMenu *>(menu)) {
                if (beaconMenu->getBeacon()) scene = new BeaconScene(menu, beaconMenu->getBeacon());
            }
            break;
        case ContainerOpenPacket::REPAIR_TABLE:
            if (AnvilMenu *anvilMenu = dynamic_cast<AnvilMenu *>(menu)) {
                scene = new AnvilScene(anvilMenu, player->inventory);
            }
            break;
        case ContainerOpenPacket::TRADER_NPC:
            if (MerchantMenu *merchantMenu = dynamic_cast<MerchantMenu *>(menu)) {
                scene = mcle_trading_scene(merchantMenu, player, g_packet.title);
            }
            break;
        case ContainerOpenPacket::HORSE: {
            std::shared_ptr<EntityHorse> horse =
                std::dynamic_pointer_cast<EntityHorse>(player->level->getEntity(g_packet.entityId));
            if (dynamic_cast<HorseInventoryMenu *>(menu) && horse) {
                scene = new HorseScene(menu, player->inventory, container, horse, horseName(g_packet, horse));
            }
        } break;
        case ContainerOpenPacket::FIREWORKS:
            if (dynamic_cast<FireworksMenu *>(menu)) scene = new FireworksScene(menu);
            break;
        }
    }
    if (!scene) {
        mcle_game_set_menu_displayed(false);
        return false;
    }

    // ServerPlayer's open adds itself as a listener so slotChanged can send
    // the client every slot that changes. The client is this player, like
    // the inventory menu, which never gets the listener.
    menu->removeSlotListener(player.get());
    mcleContainerUI.PlayUISFX(eSFX_Press);
    mcle_container_begin(scene);
    return true;
}
