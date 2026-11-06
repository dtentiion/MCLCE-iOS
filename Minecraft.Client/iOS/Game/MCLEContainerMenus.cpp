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
#include "../../../upstream/Minecraft.World/ContainerOpenPacket.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_ContainerMenu.h"

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

// A scene over a container the server opened. ServerPlayer::tick closes it
// when it stops being valid, and ClientConnection::handleContainerClose
// closes the menu after it.
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
};

// UIScene_ContainerMenu
class ChestScene : public ServerContainerScene, public IUIScene_ContainerMenu {
public:
    ChestScene(AbstractContainerMenu *menu, const std::wstring &name)
        : m_slotListContainer("MainPanel.containerList"), m_labelChest("MainPanel.chestLabel") {
        mapSharedControls();
        m_slotListContainer.rect = rectAt("MainPanel.containerList");

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

} // namespace

void mcle_container_set_rect_reader(MCLEControlRect (*reader)(const char *path)) {
    g_rectReader = reader;
}

bool mcle_container_take_server_packet(int type, int containerId, const std::wstring &title, bool customName,
                                       int size) {
    if (!isChest(type)) return false;
    g_packet.type = type;
    g_packet.containerId = containerId;
    g_packet.title = title;
    g_packet.customName = customName;
    g_packet.size = size;
    return true;
}

// CMinecraftApp::LoadContainerMenu picks the large chest movie past three
// rows
const char *mcle_container_server_movie() {
    if (isChest(g_packet.type)) return g_packet.size > 3 * 9 ? "ChestLargeMenu1080.swf" : "ChestMenu1080.swf";
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

    MCLEContainerScene *scene = nullptr;
    if (isChest(g_packet.type) && dynamic_cast<ContainerMenu *>(menu) != nullptr) {
        mcle_container_prepare();
        scene = new ChestScene(menu, chestName(g_packet));
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
