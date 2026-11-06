// The villager trading menu, ClientConnection::handleContainerOpen's
// TRADER_NPC. IUIScene_TradingMenu, the offer bar and the trade, is
// upstream's own: its body is copied out by scripts/gen-trading-menu.py and
// compiled below. TradingScene stands in for UIScene_TradingMenu. Like the
// client it trades from a ClientSideMerchant whose offers come in the
// server's MC|TrList, over its own MerchantMenu, while the server's
// MerchantMenu stays on the player.

#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"
#include "MCLETutorial.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "../../../upstream/Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.trading.h"
#include "../../../upstream/Minecraft.World/net.minecraft.stats.h"
#include "../../../upstream/Minecraft.World/ClientSideMerchant.h"
#include "../../../upstream/Minecraft.World/CustomPayloadPacket.h"
#include "../../../upstream/Minecraft.World/InputOutputStream.h"
#include "../../../upstream/Minecraft.World/HtmlString.h"
#include "../../../upstream/Minecraft.World/StringHelpers.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/Options.h"
#include "../../../upstream/Minecraft.Client/Common/Tutorial/Tutorial.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_TradingMenu.h"

namespace {

// The last MC|TrList the server sent, until the menu it's for takes it
std::mutex g_traderListMutex;
std::vector<unsigned char> g_traderList;
bool g_haveTraderList = false;

// PlayerConnection::handleTradeItem, on the server's own player
void tradeItem(const std::shared_ptr<ServerPlayer> &player, int containerId, int offer) {
    if (player->containerMenu->containerId == containerId) {
        MerchantMenu *menu = static_cast<MerchantMenu *>(player->containerMenu);

        MerchantRecipeList *offers = menu->getMerchant()->getOffers(player);

        if (offers) {
            int selectedShopItem = offer;
            if (selectedShopItem < offers->size()) {
                MerchantRecipe *activeRecipe = offers->at(selectedShopItem);
                if (!activeRecipe->isDeprecated()) {
                    // Do we have the ingredients?
                    shared_ptr<ItemInstance> buyAItem = activeRecipe->getBuyAItem();
                    shared_ptr<ItemInstance> buyBItem = activeRecipe->getBuyBItem();

                    int buyAMatches = player->inventory->countMatches(buyAItem);
                    int buyBMatches = player->inventory->countMatches(buyBItem);
                    if ((buyAItem != nullptr && buyAMatches >= buyAItem->count) &&
                        (buyBItem == nullptr || buyBMatches >= buyBItem->count)) {
                        menu->getMerchant()->notifyTrade(activeRecipe);

                        // Remove the items we are purchasing with
                        player->inventory->removeResources(buyAItem);
                        player->inventory->removeResources(buyBItem);

                        // Add the item we have purchased
                        shared_ptr<ItemInstance> result = activeRecipe->getSellItem()->copy();

                        // 4J JEV - Award itemsBought stat.
                        player->awardStat(GenericStats::itemsBought(result->getItem()->id),
                                          GenericStats::param_itemsBought(result->getItem()->id,
                                                                          result->getAuxValue(), result->GetCount()));

                        if (!player->inventory->add(result)) {
                            player->drop(result);
                        }
                    }
                }
            }
        }
    }
}

} // namespace

// The client's player, the local player's ServerPlayer here
static std::shared_ptr<ServerPlayer> mcle_trading_player() {
    return mcle_local_player();
}

// The TradeItemPacket goes to the server, which is this same player. The
// stat it awards sends through the connection.
static void mcle_trade_item(int containerId, int offer) {
    struct Trade {
        std::shared_ptr<ServerPlayer> player;
        int containerId, offer;
    } trade{mcle_trading_player(), containerId, offer};
    if (!trade.player) return;
    mcle_run_with_stand_in_connection(trade.player, [](void *arg) {
        Trade *t = static_cast<Trade *>(arg);
        tradeItem(t->player, t->containerId, t->offer);
    }, &trade);
}

// PlayerConnection::handleCustomPayload's TRADER_SELECTION_PACKET, on the
// same player's menu
static void mcle_trader_selection(byteArray data) {
    ByteArrayInputStream bais(data);
    DataInputStream input(&bais);
    int selection = input.readInt();

    std::shared_ptr<ServerPlayer> player = mcle_trading_player();
    if (!player) return;
    AbstractContainerMenu *menu = player->containerMenu;
    if (dynamic_cast<MerchantMenu *>(menu)) {
        static_cast<MerchantMenu *>(menu)->setSelectionHint(selection);
    }
}

#define ui mcleContainerUI
#include "../../../upstream/Minecraft.Client/iOS_TradingMenu.inc"
#undef ui

namespace {

// UIScene_TradingMenu
class TradingScene : public MCLEContainerScene, public IUIScene_TradingMenu {
public:
    TradingScene(MerchantMenu *serverMenu, const std::shared_ptr<ServerPlayer> &player, const std::wstring &title)
        : m_serverMenu(serverMenu), m_slotListTrades("MainPanel.TradingBar"),
          m_slotListRequest1("MainPanel.Request1"), m_slotListRequest2("MainPanel.Request2"),
          m_slotListHotbar("MainPanel.HotBar"), m_slotListInventory("MainPanel.Inventory"),
          m_labelInventory("MainPanel.InventoryLabel"), m_labelTrading("MainPanel.VillagerText"),
          m_labelRequired("MainPanel.RequiredLabel"), m_labelRequest1("MainPanel.Request1Label"),
          m_labelRequest2("MainPanel.Request2Label") {
        m_showingLeftArrow = true;
        m_showingRightArrow = true;

        // 4J-PB - "Villager" appears for a short time on opening the trading menu
        m_labelTrading.init(L"");
        m_labelInventory.init(app.GetString(IDS_INVENTORY));
        m_labelRequired.init(app.GetString(IDS_REQUIRED_ITEMS_FOR_TRADE));

        m_labelRequest1.init(L"");
        m_labelRequest2.init(L"");

        // handleContainerOpen's ClientSideMerchant. Its createContainer
        // container is never used, and holding the merchant it would keep
        // it and the server player alive, so it's left out.
        m_clientMerchant = std::make_shared<ClientSideMerchant>(player, title);
        m_merchant = m_clientMerchant;

        if (Tutorial *tutorial = mcle_tutorial()) {
            m_previousTutorialState = tutorial->getCurrentState();
            tutorial->changeTutorialState(e_Tutorial_State_Trading_Menu, mcle_ui_scene(this));
        }

        // The client's own menu, which takes the server's container id. The
        // player keeps the server's.
        m_menu = new MerchantMenu(player->inventory, m_merchant, player->level);
        m_menu->containerId = serverMenu->containerId;

        m_slotListRequest1.addSlots(BUY_A, 1);
        m_slotListRequest2.addSlots(BUY_B, 1);

        m_slotListTrades.addSlots(TRADES_START, DISPLAY_TRADES_COUNT);

        m_slotListInventory.addSlots(MerchantMenu::INV_SLOT_START, 27);
        m_slotListHotbar.addSlots(MerchantMenu::USE_ROW_SLOT_START, 9);

        // ui.OverrideSFX: UIComponent_Tooltips plays no sound for these either way

        // updateTooltips
        mcleContainerUI.SetTooltips(getPad(), IDS_TOOLTIPS_TRADE, IDS_TOOLTIPS_BACK);

        takeTraderList();
    }

    ~TradingScene() override { delete m_menu; }

    void handleInput(int key, bool repeat, bool pressed, bool released) override {
        if (pressed) handleKeyDown(getPad(), key, repeat);
    }

    // UIScene::tick then handleTick. The server's MC|TrList and a closed
    // container come in between frames, ClientConnection's handlers for
    // them run first.
    void tick(double now) override {
        std::shared_ptr<ServerPlayer> player = mcle_trading_player();
        if (player && player->containerMenu != m_serverMenu) {
            // LocalPlayer::closeContainer
            mcleContainerUI.PlayUISFX(eSFX_Back);
            mcleContainerUI.CloseUIScenes(getPad());
            return;
        }
        takeTraderList();

        m_labelInventory.tick();
        m_labelTrading.tick();
        m_labelRequired.tick();
        m_labelRequest1.tick();
        m_labelRequest2.tick();

        handleTick();
    }

    // The tutorial goes back to its state before the menu, and the client's
    // closeContainer() is the server's handleContainerClose, doCloseContainer()
    void handleDestroy() override {
        if (Tutorial *tutorial = mcle_tutorial()) tutorial->changeTutorialState(m_previousTutorialState);

        std::shared_ptr<ServerPlayer> player = mcle_trading_player();
        if (player != nullptr && player->containerMenu != nullptr &&
            player->containerMenu->containerId == m_menu->containerId) {
            player->doCloseContainer();
        }
    }

    // What customDraw draws in each slot, as it was at the end of the frame
    void snapshot() override {
        m_slotItems.assign(TRADES_START + DISPLAY_TRADES_COUNT, nullptr);
        for (int slotId = 0; slotId < MerchantMenu::USE_ROW_SLOT_END; ++slotId) {
            Slot *slot = m_menu->getSlot(slotId);
            m_slotItems[slotId] = copyOf(slot ? slot->getItem() : nullptr);
        }
        int tradeId = m_selectedSlot + m_offersStartIndex;
        if (tradeId < static_cast<int>(m_activeOffers.size())) {
            m_slotItems[BUY_A] = copyOf(m_activeOffers.at(tradeId).first->getBuyAItem());
            m_slotItems[BUY_B] = copyOf(m_activeOffers.at(tradeId).first->getBuyBItem());
        }
        for (int i = 0; i < DISPLAY_TRADES_COUNT; ++i) {
            tradeId = i + m_offersStartIndex;
            if (tradeId < static_cast<int>(m_activeOffers.size())) {
                m_slotItems[TRADES_START + i] = copyOf(m_activeOffers.at(tradeId).first->getSellItem());
            }
        }
    }

    // UIScene_TradingMenu::customDraw
    void customDraw(const MCLECustomDrawRegion &region) override {
        int slotId = -1;
        if (sscanf(region.name, "slot_%d", &slotId) != 1) return;
        if (slotId < 0 || slotId >= static_cast<int>(m_slotItems.size())) return;
        std::shared_ptr<ItemInstance> item = m_slotItems[slotId];
        if (item != nullptr) mcle_custom_draw_slot(region, item, 1.0f, true);
    }

protected:
    MerchantMenu *m_serverMenu;
    std::shared_ptr<ClientSideMerchant> m_clientMerchant;
    bool m_showingRightArrow, m_showingLeftArrow;

    MCLEUISlotList m_slotListTrades;
    MCLEUISlotList m_slotListRequest1, m_slotListRequest2;
    MCLEUISlotList m_slotListHotbar, m_slotListInventory;
    MCLEUILabel m_labelInventory;
    MCLEUILabel m_labelTrading, m_labelRequired;
    MCLEUILabel m_labelRequest1, m_labelRequest2;

    std::vector<std::shared_ptr<ItemInstance>> m_slotItems;

    static std::shared_ptr<ItemInstance> copyOf(const std::shared_ptr<ItemInstance> &item) {
        return item != nullptr ? item->copy() : nullptr;
    }

    // ClientConnection::handleCustomPayload's TRADER_LIST_PACKET, for the
    // trading menu that's up when its container id is the client's
    void takeTraderList() {
        std::vector<unsigned char> bytes;
        {
            std::lock_guard<std::mutex> lock(g_traderListMutex);
            if (!g_haveTraderList) return;
            bytes.swap(g_traderList);
            g_haveTraderList = false;
        }
        if (bytes.size() < 4) return;
        byteArray data(static_cast<unsigned int>(bytes.size()));
        memcpy(data.data, bytes.data(), bytes.size());
        ByteArrayInputStream bais(data);
        DataInputStream input(&bais);
        int containerId = input.readInt();
        if (containerId == m_menu->containerId) {
            MerchantRecipeList *recipeList = MerchantRecipeList::createFromStream(&input);
            m_clientMerchant->overrideOffers(recipeList);
        }
    }

    int getPad() override { return 0; }

    void showScrollRightArrow(bool show) override {
        if (m_showingRightArrow != show) {
            mcle_ui_call("", "ShowScrollRightArrow", {mcle_ui_bool(show)});
            m_showingRightArrow = show;
        }
    }

    void showScrollLeftArrow(bool show) override {
        if (m_showingLeftArrow != show) {
            mcle_ui_call("", "ShowScrollLeftArrow", {mcle_ui_bool(show)});
            m_showingLeftArrow = show;
        }
    }

    void moveSelector(bool right) override { mcle_ui_call("", "MoveSelector", {mcle_ui_bool(right)}); }

    void setTitle(const wstring &name) override { m_labelTrading.setLabel(name); }
    void setRequest1Name(const wstring &name) override { m_labelRequest1.setLabel(name); }
    void setRequest2Name(const wstring &name) override { m_labelRequest2.setLabel(name); }

    void setRequest1RedBox(bool show) override { m_slotListRequest1.showSlotRedBox(0, show); }
    void setRequest2RedBox(bool show) override { m_slotListRequest2.showSlotRedBox(0, show); }
    void setTradeRedBox(int index, bool show) override { m_slotListTrades.showSlotRedBox(index, show); }

    void setOfferDescription(vector<HtmlString> *description) override {
        mcle_ui_call("", "SetOfferDescription", {mcle_ui_str(HtmlString::Compose(description))});
    }
};

} // namespace

void mcle_container_custom_payload(const std::wstring &identifier, byteArray data) {
    if (CustomPayloadPacket::TRADER_LIST_PACKET.compare(identifier) != 0) return;
    std::lock_guard<std::mutex> lock(g_traderListMutex);
    g_traderList.assign(data.data, data.data + (data.data ? data.length : 0));
    g_haveTraderList = true;
}

MCLEContainerScene *mcle_trading_scene(MerchantMenu *menu, const std::shared_ptr<ServerPlayer> &player,
                                       const std::wstring &title) {
    return new TradingScene(menu, player, title);
}
