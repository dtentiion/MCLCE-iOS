// The creative menu. IUIScene_CreativeMenu, the item table, the tabs, the
// paging and the picking, is upstream's own: its body is copied out by
// scripts/gen-creative-menu.py and compiled below. CreativeScene stands in
// for UIScene_CreativeMenu.

#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"
#include "MCLEAbstractContainerScene.h"

#include <algorithm>
#include <cassert>
#include <mutex>

#include "../../../upstream/Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.level.tile.h"
#include "../../../upstream/Minecraft.World/SkullTileEntity.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.enchantment.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.entity.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.entity.animal.h"
#include "../../../upstream/Minecraft.World/ItemEntity.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/Dimension.h"
#include "../../../upstream/Minecraft.World/SimpleContainer.h"
#include "../../../upstream/Minecraft.World/MapItem.h"
#include "../../../upstream/Minecraft.World/MapItemSavedData.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/JavaMath.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/ServerPlayerGameMode.h"
#include "../../../upstream/Minecraft.Client/Common/Potion_Macros.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_CreativeMenu.h"

namespace {

// PlayerConnection::dropSpamTickCount, which its tick takes one off
int g_dropSpamTickCount = 0;
int64_t g_dropSpamGameTime = 0;

struct CreativeSlot {
    std::shared_ptr<ServerPlayer> player;
    int slotNum;
    std::shared_ptr<ItemInstance> item;
};

// PlayerConnection::handleSetCreativeModeSlot
void setCreativeModeSlot(void *arg) {
    CreativeSlot *packet = static_cast<CreativeSlot *>(arg);
    const std::shared_ptr<ServerPlayer> &player = packet->player;
    if (player->gameMode->isCreative()) {
        bool drop = packet->slotNum < 0;
        shared_ptr<ItemInstance> item = packet->item;

        if (item != nullptr && item->id == Item::map_Id) {
            int mapScale = 3;
#ifdef _LARGE_WORLDS
            int scale = MapItemSavedData::MAP_SIZE * 2 * (1 << mapScale);
            int centreXC = static_cast<int>(Math::round(player->x / scale) * scale);
            int centreZC = static_cast<int>(Math::round(player->z / scale) * scale);
#else
            // 4J-PB - for Xbox maps, we'll centre them on the origin of the world, since we can fit the whole world in our map
            int centreXC = 0;
            int centreZC = 0;
#endif
            item->setAuxValue(player->level->getAuxValueForMap(player->getXuid(), player->dimension, centreXC, centreZC, mapScale));

            shared_ptr<MapItemSavedData> data = MapItem::getSavedData(item->getAuxValue(), player->level);
            // 4J Stu - We only have one map per player per dimension, so don't reset the one that they have
            // when a new one is created
            wchar_t buf[64];
            swprintf(buf, 64, L"map_%d", item->getAuxValue());
            std::wstring id = wstring(buf);
            if (data == nullptr) {
                data = std::make_shared<MapItemSavedData>(id);
            }
            player->level->setSavedData(id, (shared_ptr<SavedData>)data);

            data->scale = mapScale;
            // 4J-PB - for Xbox maps, we'll centre them on the origin of the world, since we can fit the whole world in our map
            data->x = centreXC;
            data->z = centreZC;
            data->dimension = static_cast<byte>(player->level->dimension->id);
            data->setDirty();
        }

        bool validSlot = (packet->slotNum >= InventoryMenu::CRAFT_SLOT_START &&
                          packet->slotNum < (InventoryMenu::USE_ROW_SLOT_START + Inventory::getSelectionSize()));
        bool validItem = item == nullptr || (item->id < Item::items.length && item->id >= 0 && Item::items[item->id] != nullptr);
        bool validData = item == nullptr || (item->getAuxValue() >= 0 && item->count > 0 && item->count <= 64);

        if (validSlot && validItem && validData) {
            if (item == nullptr) {
                player->inventoryMenu->setItem(packet->slotNum, nullptr);
            } else {
                player->inventoryMenu->setItem(packet->slotNum, item);
            }
            player->inventoryMenu->setSynched(player, true);
        } else if (drop && validItem && validData) {
            int64_t now = player->level->getGameTime();
            g_dropSpamTickCount = std::max<int>(0, g_dropSpamTickCount - static_cast<int>(now - g_dropSpamGameTime));
            g_dropSpamGameTime = now;
            if (g_dropSpamTickCount < SharedConstants::TICKS_PER_SECOND * 10) {
                g_dropSpamTickCount += SharedConstants::TICKS_PER_SECOND;
                // drop item
                shared_ptr<ItemEntity> dropped = player->drop(item);
                if (dropped != nullptr) {
                    dropped->setShortLifeTime();
                }
            }
        }

        if (item != nullptr && item->id == Item::map_Id) {
            // 4J Stu - Maps need to have their aux value update, so the client should always be assumed to be wrong
            vector<shared_ptr<ItemInstance>> items;
            for (unsigned int i = 0; i < player->inventoryMenu->slots.size(); i++) {
                items.push_back(player->inventoryMenu->slots.at(i)->getItem());
            }
            player->refreshContainer(player->inventoryMenu, &items);
        }
    }
}

void sendCreativeModeSlot(int slotNum, const shared_ptr<ItemInstance> &item) {
    CreativeSlot packet{mcle_local_player(), slotNum, item != nullptr ? item->copy() : nullptr};
    if (!packet.player) return;
    mcle_run_with_stand_in_connection(packet.player, setCreativeModeSlot, &packet);
}

} // namespace

// The client's inventory and clicks; here the player is the server's
static std::shared_ptr<Player> mcle_container_player() {
    return mcle_local_player();
}

// MultiPlayerGameMode::handleCreativeModeItemAdd and ItemDrop send a
// SetCreativeModeSlotPacket with a copy of the item to the server, which
// is this same player
static void mcle_creative_item_add(shared_ptr<ItemInstance> clicked, int slot) {
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (player && player->gameMode->isCreative()) sendCreativeModeSlot(slot, clicked);
}

static void mcle_creative_item_drop(shared_ptr<ItemInstance> clicked) {
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (player && player->gameMode->isCreative() && clicked != nullptr) sendCreativeModeSlot(-1, clicked);
}

#define ui mcleContainerUI
#include "../../../upstream/Minecraft.Client/iOS_CreativeMenu.inc"
#undef ui

namespace {

// UIScene_CreativeMenu
class CreativeScene : public MCLEAbstractContainerScene, public IUIScene_CreativeMenu {
public:
    CreativeScene(const MCLECreativeLayout &layout, const std::shared_ptr<ServerPlayer> &player)
        : m_slotListContainer("MainPanel.containerList"), m_layout(layout) {
        m_backgroundPanel = layout.backgroundPanel;
        m_slotListHotbar.rect = layout.hotbarList;
        m_slotListContainer.rect = layout.containerList;

        int containerSize = TabSpec::MAX_SIZE;
        shared_ptr<SimpleContainer> creativeContainer = std::make_shared<SimpleContainer>(0, L"", false, containerSize);
        itemPickerMenu = new ItemPickerMenu(creativeContainer, player->inventory);

        Initialize(0, itemPickerMenu, false, -1, eSectionInventoryCreativeUsing, eSectionInventoryCreativeMax, false);

        m_labelInventory.setLabel(L"");

        for (unsigned int i = 0; i < TabSpec::MAX_SIZE; ++i) {
            m_slotListContainer.addSlot(i);
        }

        for (unsigned int i = TabSpec::MAX_SIZE; i < TabSpec::MAX_SIZE + 9; ++i) {
            m_slotListHotbar.addSlot(i);
        }

        enterTutorialState(e_Tutorial_State_Creative_Inventory_Menu);

        m_curTab = eCreativeInventoryTab_COUNT;
        switchTab(eCreativeInventoryTab_BuildingBlocks);
    }

    // The picker is the client's own menu, nothing else holds it
    ~CreativeScene() override { delete itemPickerMenu; }

    // 4J-PB - going to ignore repeats on this scene. Its VK_PAD_ shoulder and
    // trigger cases never match a menu action, so the rest is the container
    // menu's input, which hands LB, RB and the right stick to
    // handleAdditionalKeyPress.
    void handleInput(int key, bool repeat, bool pressed, bool released) override {
        if (repeat) return;
        MCLEAbstractContainerScene::handleInput(key, repeat, pressed, released);
    }

    // MultiplayerLocalPlayer::closeContainer: the client drops what the
    // pointer carried, and the server's handleContainerClose closes its own
    // container, which the picker never was
    void handleDestroy() override {
        leaveTutorialState();

        std::shared_ptr<ServerPlayer> player = mcle_local_player();
        if (!player) return;
        player->inventory->setCarried(nullptr);
        player->doCloseContainer();
    }

protected:
    MCLEUISlotList m_slotListContainer;
    MCLECreativeLayout m_layout;

    void handleOtherClicked(int iPad, ESceneSection eSection, int buttonNum, bool quickKey) override {
        switch (eSection) {
        case eSectionInventoryCreativeTab_0:
        case eSectionInventoryCreativeTab_1:
        case eSectionInventoryCreativeTab_2:
        case eSectionInventoryCreativeTab_3:
        case eSectionInventoryCreativeTab_4:
        case eSectionInventoryCreativeTab_5:
        case eSectionInventoryCreativeTab_6:
        case eSectionInventoryCreativeTab_7: {
            ECreativeInventoryTabs tab = static_cast<ECreativeInventoryTabs>(
                (int)eCreativeInventoryTab_BuildingBlocks + (int)eSection - (int)eSectionInventoryCreativeTab_0);
            if (tab != m_curTab) {
                switchTab(tab);
                mcleContainerUI.PlayUISFX(eSFX_Focus);
            }
        } break;
        case eSectionInventoryCreativeSlider:
            ScrollBar(this->m_pointerPos);
            break;
        default:
            break;
        }
    }

    void updateTabHighlightAndText(ECreativeInventoryTabs tab) override {
        mcle_ui_call("", "SetActiveTab", {mcle_ui_num(static_cast<double>(tab))});
        m_labelInventory.setLabel(app.GetString(specs[tab]->m_descriptionId));
    }

    void updateScrollCurrentPage(int currentPage, int pageCount) override {
        mcle_ui_call("", "SetScrollBar", {mcle_ui_num(pageCount), mcle_ui_num(static_cast<double>(currentPage) - 1)});
    }

    int getSectionColumns(ESceneSection eSection) override {
        int cols = 0;
        switch (eSection) {
        case eSectionInventoryCreativeSelector: cols = 10; break;
        case eSectionInventoryCreativeUsing: cols = 9; break;
        default: assert(false); break;
        }
        return cols;
    }

    int getSectionRows(ESceneSection eSection) override {
        int rows = 0;
        switch (eSection) {
        case eSectionInventoryCreativeSelector: rows = 5; break;
        case eSectionInventoryCreativeUsing: rows = 1; break;
        default: assert(false); break;
        }
        return rows;
    }

    const MCLEControlRect *sectionRect(ESceneSection eSection) const {
        switch (eSection) {
        case eSectionInventoryCreativeSelector: return &m_slotListContainer.rect;
        case eSectionInventoryCreativeUsing: return &m_slotListHotbar.rect;
        case eSectionInventoryCreativeTab_0:
        case eSectionInventoryCreativeTab_1:
        case eSectionInventoryCreativeTab_2:
        case eSectionInventoryCreativeTab_3:
        case eSectionInventoryCreativeTab_4:
        case eSectionInventoryCreativeTab_5:
        case eSectionInventoryCreativeTab_6:
        case eSectionInventoryCreativeTab_7:
            return &m_layout.tabs[eSection - eSectionInventoryCreativeTab_0];
        case eSectionInventoryCreativeSlider: return &m_layout.slider;
        default: assert(false); return nullptr;
        }
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

    void handleSectionClick(ESceneSection eSection) override {}

    void setSectionSelectedSlot(ESceneSection eSection, int x, int y) override {
        int cols = getSectionColumns(eSection);
        int index = (y * cols) + x;
        if (MCLEUISlotList *slotList = getSection(eSection)) slotList->setHighlightSlot(index);
    }

    // The tabs and the slider are touch panels, which take no focus
    MCLEUISlotList *getSection(ESceneSection eSection) override {
        switch (eSection) {
        case eSectionInventoryCreativeSelector: return &m_slotListContainer;
        case eSectionInventoryCreativeUsing: return &m_slotListHotbar;
        default: return nullptr;
        }
    }
};

} // namespace

// Minecraft::tick's crafting action in creative: the press, then
// app.LoadCreativeMenu
void mcle_container_open_creative(const MCLECreativeLayout &layout) {
    if (mcle_container_is_open()) return;
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player) return;
    mcle_container_prepare();
    mcleContainerUI.PlayUISFX(eSFX_Press);
    mcle_container_begin(new CreativeScene(layout, player));
}

// Minecraft::main builds the item table once, after the world's tables
void mcle_creative_menu_static_ctor() {
    IUIScene_CreativeMenu::staticCtor();
}
