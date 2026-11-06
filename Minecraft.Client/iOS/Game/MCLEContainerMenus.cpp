// The containers the server opens on the player, the cases of
// ClientConnection::handleContainerOpen. The client opens its own copy of
// the container and a menu over it; here the scene runs on the menu the
// server put on the player, which that copy would mirror. Each scene
// stands in for its UIScene_ class.

#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"
#include "MCLEAbstractContainerScene.h"

#include <cassert>
#include <mutex>
#include <string>

#include "../../../upstream/Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../upstream/Minecraft.World/FurnaceTileEntity.h"
#include "../../../upstream/Minecraft.World/BrewingStandTileEntity.h"
#include "../../../upstream/Minecraft.World/PotionBrewing.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/ContainerOpenPacket.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_ContainerMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_FurnaceMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_DispenserMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_HopperMenu.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_BrewingMenu.h"

namespace {

// The ContainerOpenPacket the scene is for
struct OpenPacket {
    int type = 0;
    int containerId = -1;
    std::wstring title;
    bool customName = false;
    int size = 0;
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

        // The tutorial state is left out, localgameModes is never set here

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

        // The tutorial state is left out, localgameModes is never set here

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

        // The tutorial state is left out, localgameModes is never set here

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

        // The tutorial state is left out, localgameModes is never set here

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

        // The tutorial state is left out, localgameModes is never set here

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
                                       int size) {
    if (!hasScene(type)) return false;
    g_packet.type = type;
    g_packet.containerId = containerId;
    g_packet.title = title;
    g_packet.customName = customName;
    g_packet.size = size;
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
        }
    }
    if (!scene) return false;

    // ServerPlayer's open adds itself as a listener so slotChanged can send
    // the client every slot that changes. The client is this player, like
    // the inventory menu, which never gets the listener.
    menu->removeSlotListener(player.get());
    mcleContainerUI.PlayUISFX(eSFX_Press);
    mcle_container_begin(scene);
    return true;
}
