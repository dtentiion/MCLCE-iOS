// The crafting menus, the 2x2 from the crafting button and the crafting
// table's 3x3. IUIScene_CraftingMenu, the recipe groups, slots, ingredients
// and the crafting, is upstream's own: its body is copied out by
// scripts/gen-crafting-menu.py and compiled below. CraftingScene stands in
// for UIScene_CraftingMenu, keeping what each slot shows for customDraw,
// and its Iggy calls are queued like the other container menus'.

#include "MCLEContainerMenu.h"
#include "MCLEContainerScene.h"
#include "MCLETutorial.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "../../../upstream/Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.item.crafting.h"
#include "../../../upstream/Minecraft.World/net.minecraft.world.level.tile.h"
#include "../../../upstream/Minecraft.World/net.minecraft.stats.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_CraftingMenu.h"

std::wstring mcle_format_html_string(int iPad, const std::wstring &desc); // MCLEHowToPlay.cpp

namespace {

// The player the open scene crafts for
std::shared_ptr<ServerPlayer> g_craftingPlayer;

// PlayerConnection::handleCraftItem, the crafting the client asks the
// server for. `player` is the server's own.
void craftItem(const std::shared_ptr<ServerPlayer> &player, int iRecipe) {
    if (iRecipe == -1) return;

    int recipeCount = (int)Recipes::getInstance()->getRecipies()->size();
    if (iRecipe < 0 || iRecipe >= recipeCount) return;

    Recipy::INGREDIENTS_REQUIRED *pRecipeIngredientsRequired = Recipes::getInstance()->getRecipeIngredientsArray();
    shared_ptr<ItemInstance> pTempItemInst = pRecipeIngredientsRequired[iRecipe].pRecipy->assemble(nullptr);

    if (app.DebugSettingsOn() && (player->GetDebugOptions() & (1L << eDebugSetting_CraftAnything))) {
        pTempItemInst->onCraftedBy(player->level, dynamic_pointer_cast<Player>(player->shared_from_this()),
                                   pTempItemInst->count);
        if (player->inventory->add(pTempItemInst) == false) {
            // no room in inventory, so throw it down
            player->drop(pTempItemInst);
        }
    } else if (pTempItemInst->id == Item::fireworksCharge_Id || pTempItemInst->id == Item::fireworks_Id) {
        // openFireworks swaps the crafting table for the fireworks menu,
        // which isn't here yet, so nothing is made
        app.DebugPrintf("fireworks need the fireworks menu\n");
        return;
    } else {
        Recipy::INGREDIENTS_REQUIRED &req = pRecipeIngredientsRequired[iRecipe];
        if (req.iType == RECIPE_TYPE_3x3 && dynamic_cast<CraftingMenu *>(player->containerMenu) == nullptr) {
            app.DebugPrintf("tried to craft a 3x3 recipe without a crafting bench\n");
            return;
        }
        for (int i = 0; i < req.iIngC; i++) {
            int need = req.iIngValA[i];
            int have = player->inventory->countResource(req.iIngIDA[i], req.iIngAuxValA[i]);
            if (have < need) {
                app.DebugPrintf("tried to craft item %d with insufficient ingredients\n", pTempItemInst->id);
                return;
            }
        }

        pTempItemInst->onCraftedBy(player->level, dynamic_pointer_cast<Player>(player->shared_from_this()),
                                   pTempItemInst->count);

        // and remove those resources from your inventory
        for (int i = 0; i < pRecipeIngredientsRequired[iRecipe].iIngC; i++) {
            for (int j = 0; j < pRecipeIngredientsRequired[iRecipe].iIngValA[i]; j++) {
                shared_ptr<ItemInstance> ingItemInst = nullptr;
                // do we need to remove a specific aux value?
                if (pRecipeIngredientsRequired[iRecipe].iIngAuxValA[i] != Recipes::ANY_AUX_VALUE) {
                    ingItemInst = player->inventory->getResourceItem(pRecipeIngredientsRequired[iRecipe].iIngIDA[i],
                                                                     pRecipeIngredientsRequired[iRecipe].iIngAuxValA[i]);
                    player->inventory->removeResource(pRecipeIngredientsRequired[iRecipe].iIngIDA[i],
                                                      pRecipeIngredientsRequired[iRecipe].iIngAuxValA[i]);
                } else {
                    ingItemInst = player->inventory->getResourceItem(pRecipeIngredientsRequired[iRecipe].iIngIDA[i]);
                    player->inventory->removeResource(pRecipeIngredientsRequired[iRecipe].iIngIDA[i]);
                }

                // 4J Stu - Fix for #13097 - Bug: Milk Buckets are removed when crafting Cake
                if (ingItemInst != nullptr) {
                    if (ingItemInst->getItem()->hasCraftingRemainingItem()) {
                        // replace item with remaining result
                        player->inventory->add(
                            std::make_shared<ItemInstance>(ingItemInst->getItem()->getCraftingRemainingItem()));
                    }
                }
            }
        }

        // 4J Stu - Fix for #13119 - We should add the item after we remove the ingredients
        if (player->inventory->add(pTempItemInst) == false) {
            // no room in inventory, so throw it down
            player->drop(pTempItemInst);
        }

        if (pTempItemInst->id == Item::map_Id) {
            // 4J Stu - Maps need to have their aux value update, so the client should always be assumed to be wrong
            vector<shared_ptr<ItemInstance>> items;
            for (unsigned int i = 0; i < player->containerMenu->slots.size(); i++) {
                items.push_back(player->containerMenu->slots.at(i)->getItem());
            }
            player->refreshContainer(player->containerMenu, &items);
        } else {
            // Do same hack as PlayerConnection::handleContainerClick does
            player->ignoreSlotUpdateHack = true;
            player->containerMenu->broadcastChanges();
            player->broadcastCarriedItem();
            player->ignoreSlotUpdateHack = false;
        }
    }

    // handle achievements
    switch (pTempItemInst->id) {
    case Tile::workBench_Id:     player->awardStat(GenericStats::buildWorkbench(), GenericStats::param_buildWorkbench()); break;
    case Item::pickAxe_wood_Id:  player->awardStat(GenericStats::buildPickaxe(), GenericStats::param_buildPickaxe()); break;
    case Tile::furnace_Id:       player->awardStat(GenericStats::buildFurnace(), GenericStats::param_buildFurnace()); break;
    case Item::hoe_wood_Id:      player->awardStat(GenericStats::buildHoe(), GenericStats::param_buildHoe()); break;
    case Item::bread_Id:         player->awardStat(GenericStats::makeBread(), GenericStats::param_makeBread()); break;
    case Item::cake_Id:          player->awardStat(GenericStats::bakeCake(), GenericStats::param_bakeCake()); break;
    case Item::pickAxe_stone_Id: player->awardStat(GenericStats::buildBetterPickaxe(), GenericStats::param_buildBetterPickaxe()); break;
    case Item::sword_wood_Id:    player->awardStat(GenericStats::buildSword(), GenericStats::param_buildSword()); break;
    case Tile::dispenser_Id:     player->awardStat(GenericStats::dispenseWithThis(), GenericStats::param_dispenseWithThis()); break;
    case Tile::enchantTable_Id:  player->awardStat(GenericStats::enchantments(), GenericStats::param_enchantments()); break;
    case Tile::bookshelf_Id:     player->awardStat(GenericStats::bookcase(), GenericStats::param_bookcase()); break;
    }
}

} // namespace

// m_pPlayer is the client's LocalPlayer; here it's the server player
static const std::shared_ptr<ServerPlayer> &mcle_crafting_player() {
    return g_craftingPlayer;
}

// MultiPlayerGameMode::handleCraftItem sends the recipe to the server; the
// server is this same player, so it crafts here
static void mcle_craft_item(int iRecipe) {
    struct Craft {
        std::shared_ptr<ServerPlayer> player;
        int recipe;
    } craft{mcle_crafting_player(), iRecipe};
    mcle_run_with_stand_in_connection(craft.player, [](void *arg) {
        Craft *c = static_cast<Craft *>(arg);
        craftItem(c->player, c->recipe);
    }, &craft);
}

#define ui mcleContainerUI
#include "../../../upstream/Minecraft.Client/iOS_CraftingMenu.inc"
#undef ui

namespace {

// UIScene_CraftingMenu's slot ids, the custom draw regions' names
const int CRAFTING_INVENTORY_SLOT_START = 0;
const int CRAFTING_INVENTORY_SLOT_END = CRAFTING_INVENTORY_SLOT_START + 27;
const int CRAFTING_HOTBAR_SLOT_START = CRAFTING_INVENTORY_SLOT_END;
const int CRAFTING_HOTBAR_SLOT_END = CRAFTING_HOTBAR_SLOT_START + 9;
const int CRAFTING_INGREDIENTS_DESCRIPTION_START = CRAFTING_HOTBAR_SLOT_END;
const int CRAFTING_INGREDEINTS_DESCRIPTION_END = CRAFTING_INGREDIENTS_DESCRIPTION_START + 4;
const int CRAFTING_OUTPUT_SLOT_START = CRAFTING_INGREDEINTS_DESCRIPTION_END;
const int CRAFTING_OUTPUT_SLOT_END = CRAFTING_OUTPUT_SLOT_START + 1;
const int CRAFTING_INGREDIENTS_LAYOUT_START = CRAFTING_OUTPUT_SLOT_END;
const int CRAFTING_INGREDIENTS_LAYOUT_END = CRAFTING_INGREDIENTS_LAYOUT_START + 9;
const int CRAFTING_V_SLOT_START = CRAFTING_INGREDIENTS_LAYOUT_END;
const int CRAFTING_V_SLOT_END = CRAFTING_V_SLOT_START + 3;
const int CRAFTING_H_SLOT_START = CRAFTING_V_SLOT_END;

// UIScene::convertGameActionToIggyKeycode for the right stick
const int IGGY_KEYCODE_F11 = 122;
const int IGGY_KEYCODE_F12 = 123;

// UIScene_CraftingMenu
class CraftingScene : public MCLEContainerScene, public IUIScene_CraftingMenu {
public:
    CraftingScene(bool table, const std::shared_ptr<ServerPlayer> &player)
        : m_slotListCraftingHSlots("MainPanel.CraftingHSlots"),
          m_slotListCrafting1VSlots("MainPanel.CraftingSelector.Crafting1VSlot1"),
          m_slotListCrafting2VSlots{MCLEUISlotList("MainPanel.SlotSelector2.Crafting2VSlot1"),
                                    MCLEUISlotList("MainPanel.SlotSelector2.Crafting2VSlot2")},
          m_slotListCrafting3VSlots{MCLEUISlotList("MainPanel.SlotSelector3.Crafting3VSlot1"),
                                    MCLEUISlotList("MainPanel.SlotSelector3.Crafting3VSlot2"),
                                    MCLEUISlotList("MainPanel.SlotSelector3.Crafting3VSlot3")},
          m_slotListIngredientsLayout("MainPanel.IngredientsLayout"),
          m_slotListCraftingOutput("MainPanel.CraftingOutput"),
          m_slotListIngredients{MCLEUISlotList("MainPanel.Ingredient1"), MCLEUISlotList("MainPanel.Ingredient2"),
                                MCLEUISlotList("MainPanel.Ingredient3"), MCLEUISlotList("MainPanel.Ingredient4")},
          m_slotListInventory("MainPanel.Inventory"), m_slotListHotBar("MainPanel.HotBar"),
          m_labelIngredientsDesc{MCLEUILabel("MainPanel.Ingredient1Desc"), MCLEUILabel("MainPanel.Ingredient2Desc"),
                                 MCLEUILabel("MainPanel.Ingredient3Desc"), MCLEUILabel("MainPanel.Ingredient4Desc")},
          m_labelDescription("MainPanel.DescriptionText"), m_labelGroupName("MainPanel.GroupName"),
          m_labelItemName("MainPanel.ItemName"), m_labelInventory("MainPanel.InventoryLabel"),
          m_labelIngredients("MainPanel.IngredientsLabel") {
        m_bIgnoreKeyPresses = false;

        m_iContainerType = table ? RECIPE_TYPE_3x3 : RECIPE_TYPE_2x2;
        g_craftingPlayer = player;
        m_bSplitscreen = false;

        for (unsigned int i = 0; i < 4; ++i) m_labelIngredientsDesc[i].init(L"");
        m_labelDescription.init(L"");
        m_labelGroupName.init(L"");
        m_labelItemName.init(L"");
        m_labelInventory.init(app.GetString(IDS_INVENTORY));
        m_labelIngredients.init(app.GetString(IDS_INGREDIENTS));

        if (m_iContainerType == RECIPE_TYPE_2x2) {
            m_menu = player->inventoryMenu;
            m_iMenuInventoryStart = InventoryMenu::INV_SLOT_START;
            m_iMenuHotBarStart = InventoryMenu::USE_ROW_SLOT_START;
        } else {
            // The CraftingMenu the client makes here is a copy of the
            // server's; the server's is the player's own here
            m_menu = player->containerMenu;
            m_iMenuInventoryStart = CraftingMenu::INV_SLOT_START;
            m_iMenuHotBarStart = CraftingMenu::USE_ROW_SLOT_START;
        }
        m_slotListInventory.addSlots(CRAFTING_INVENTORY_SLOT_START,
                                     CRAFTING_INVENTORY_SLOT_END - CRAFTING_INVENTORY_SLOT_START);
        m_slotListHotBar.addSlots(CRAFTING_HOTBAR_SLOT_START, CRAFTING_HOTBAR_SLOT_END - CRAFTING_HOTBAR_SLOT_START);

        if (m_iContainerType == RECIPE_TYPE_3x3) {
            m_iIngredientsMaxSlotC = m_iIngredients3x3SlotC;
            m_pGroupA = (Recipy::_eGroupType *)&m_GroupTypeMapping9GridA;
            m_pGroupTabA = (_eGroupTab *)&m_GroupTabBkgMapping3x3A;
            m_iCraftablesMaxHSlotC = m_iMaxHSlot3x3C;
        } else {
            m_iIngredientsMaxSlotC = m_iIngredients2x2SlotC;
            m_pGroupA = (Recipy::_eGroupType *)&m_GroupTypeMapping4GridA;
            m_pGroupTabA = (_eGroupTab *)&m_GroupTabBkgMapping2x2A;
            m_iCraftablesMaxHSlotC = m_iMaxHSlot2x2C;
        }

        setGroupText(GetGroupNameText(m_pGroupA[m_iGroupIndex]));

        // Update the tutorial state
        if (Tutorial *tutorial = mcle_tutorial()) {
            m_previousTutorialState = tutorial->getCurrentState();
            if (m_iContainerType == RECIPE_TYPE_2x2) {
                tutorial->changeTutorialState(e_Tutorial_State_2x2Crafting_Menu, mcle_ui_scene(this));
            } else {
                tutorial->changeTutorialState(e_Tutorial_State_3x3Crafting_Menu, mcle_ui_scene(this));
            }
        }

        for (unsigned int i = 0; i < 4; ++i) m_slotListIngredients[i].addSlot(CRAFTING_INGREDIENTS_DESCRIPTION_START + i);
        m_slotListCraftingOutput.addSlot(CRAFTING_OUTPUT_SLOT_START);
        m_slotListIngredientsLayout.addSlots(CRAFTING_INGREDIENTS_LAYOUT_START, m_iIngredientsMaxSlotC);

        // 3 Slot vertical scroll
        m_slotListCrafting3VSlots[0].addSlot(CRAFTING_V_SLOT_START + 0);
        m_slotListCrafting3VSlots[1].addSlot(CRAFTING_V_SLOT_START + 1);
        m_slotListCrafting3VSlots[2].addSlot(CRAFTING_V_SLOT_START + 2);

        // 2 Slot vertical scroll
        // 2 slot scroll has swapped order
        m_slotListCrafting2VSlots[0].addSlot(CRAFTING_V_SLOT_START + 1);
        m_slotListCrafting2VSlots[1].addSlot(CRAFTING_V_SLOT_START + 0);

        // 1 Slot scroll (for 480 mainly)
        m_slotListCrafting1VSlots.addSlot(CRAFTING_V_SLOT_START);

        m_slotListCraftingHSlots.addSlots(CRAFTING_H_SLOT_START, m_iCraftablesMaxHSlotC);

        // Check which recipes are available with the resources we have
        CheckRecipesAvailable();
        // reset the vertical slots
        iVSlotIndexA[0] = CanBeMadeA[m_iCurrentSlotHIndex].iCount - 1;
        iVSlotIndexA[1] = 0;
        iVSlotIndexA[2] = 1;
        UpdateVerticalSlots();
        UpdateHighlight();

        // ui.OverrideSFX: UIComponent_Tooltips plays no sound for these either way

        // 4J-PB - Must be after the CanBeMade list has been set up with CheckRecipesAvailable
        UpdateTooltips();
    }

    // X is used to open this menu, so don't let it repeat
    bool allowRepeat(int key) override { return key != ACTION_MENU_X; }

    // XuiCraftingTask's UIScene_CraftingMenu
    IUIScene_CraftingMenu *craftingMenu() override { return this; }

    void handleInput(int key, bool repeat, bool pressed, bool released) override {
        switch (key) {
        case ACTION_MENU_OTHER_STICK_UP:
        case ACTION_MENU_OTHER_STICK_DOWN:
            // sendInputToMovie, the description scrolls on F11 / F12
            mcle_ui_key(key == ACTION_MENU_OTHER_STICK_UP ? IGGY_KEYCODE_F11 : IGGY_KEYCODE_F12, pressed);
            break;
        default:
            if (pressed) handleKeyDown(getPad(), key, repeat);
            break;
        }
    }

    // UIScene::tick sends the labels that changed. ServerPlayer::tick
    // closes a crafting table that has gone or been walked away from, and
    // ClientConnection::handleContainerClose closes the menu after it.
    void tick(double now) override {
        for (MCLEUILabel &label : m_labelIngredientsDesc) label.tick();
        m_labelDescription.tick();
        m_labelGroupName.tick();
        m_labelItemName.tick();
        m_labelInventory.tick();
        m_labelIngredients.tick();

        if (m_iContainerType == RECIPE_TYPE_3x3 && g_craftingPlayer && g_craftingPlayer->containerMenu != m_menu) {
            mcleContainerUI.CloseUIScenes(getPad());
        }
    }

    // The tutorial goes back to its state before the menu. As for the
    // inventory, the client's closeContainer() is the server's
    // doCloseContainer() here.
    void handleDestroy() override {
        if (Tutorial *tutorial = mcle_tutorial()) tutorial->changeTutorialState(m_previousTutorialState);

        if (g_craftingPlayer != nullptr && g_craftingPlayer->containerMenu != nullptr &&
            g_craftingPlayer->containerMenu->containerId == m_menu->containerId) {
            g_craftingPlayer->doCloseContainer();
        }
        g_craftingPlayer = nullptr;
    }

    // The inventory and hotbar as they were at the end of the frame
    void snapshot() override {
        for (int i = 0; i < CRAFTING_HOTBAR_SLOT_END; ++i) {
            int index = i < CRAFTING_INVENTORY_SLOT_END ? m_iMenuInventoryStart + i
                                                        : m_iMenuHotBarStart + (i - CRAFTING_HOTBAR_SLOT_START);
            Slot *slot = m_menu ? m_menu->getSlot(index) : nullptr;
            std::shared_ptr<ItemInstance> item = slot ? slot->getItem() : nullptr;
            m_inventoryItems[i] = item != nullptr ? item->copy() : nullptr;
        }
    }

    // UIScene_CraftingMenu::customDraw
    void customDraw(const MCLECustomDrawRegion &region) override {
        shared_ptr<ItemInstance> item = nullptr;
        int slotId = -1;
        float alpha = 1.0f;
        bool decorations = true;
        bool inventoryItem = false;
        if (sscanf(region.name, "slot_%d", &slotId) != 1) slotId = -1;
        if (slotId == -1) {
            // not a slot
        } else if (slotId >= CRAFTING_INVENTORY_SLOT_START && slotId < CRAFTING_HOTBAR_SLOT_END) {
            item = m_inventoryItems[slotId];
            inventoryItem = true;
        } else if (slotId >= CRAFTING_V_SLOT_START && slotId < CRAFTING_V_SLOT_END) {
            decorations = false;
            int iIndex = slotId - CRAFTING_V_SLOT_START;
            if (m_vSlotsInfo[iIndex].show) {
                item = m_vSlotsInfo[iIndex].item;
                alpha = static_cast<float>(m_vSlotsInfo[iIndex].alpha) / 31.0f;
            }
        } else if (slotId >= CRAFTING_H_SLOT_START && slotId < (CRAFTING_H_SLOT_START + m_iCraftablesMaxHSlotC)) {
            decorations = false;
            int iIndex = slotId - CRAFTING_H_SLOT_START;
            if (m_hSlotsInfo[iIndex].show) {
                item = m_hSlotsInfo[iIndex].item;
                alpha = static_cast<float>(m_hSlotsInfo[iIndex].alpha) / 31.0f;
            }
        } else if (slotId >= CRAFTING_INGREDIENTS_LAYOUT_START &&
                   slotId < (CRAFTING_INGREDIENTS_LAYOUT_START + m_iIngredientsMaxSlotC)) {
            int iIndex = slotId - CRAFTING_INGREDIENTS_LAYOUT_START;
            if (m_ingredientsSlotsInfo[iIndex].show) {
                item = m_ingredientsSlotsInfo[iIndex].item;
                alpha = static_cast<float>(m_ingredientsSlotsInfo[iIndex].alpha) / 31.0f;
            }
        } else if (slotId >= CRAFTING_INGREDIENTS_DESCRIPTION_START &&
                   slotId < (CRAFTING_INGREDIENTS_DESCRIPTION_START + 4)) {
            int iIndex = slotId - CRAFTING_INGREDIENTS_DESCRIPTION_START;
            if (m_ingredientsInfo[iIndex].show) {
                item = m_ingredientsInfo[iIndex].item;
                alpha = static_cast<float>(m_ingredientsInfo[iIndex].alpha) / 31.0f;
            }
        } else if (slotId == CRAFTING_OUTPUT_SLOT_START) {
            if (m_craftingOutputSlotInfo.show) {
                item = m_craftingOutputSlotInfo.item;
                alpha = static_cast<float>(m_craftingOutputSlotInfo.alpha) / 31.0f;
            }
        }

        if (item != nullptr) {
            if (!inventoryItem) {
                if (item->id == Item::clock_Id || item->id == Item::compass_Id) {
                    // 4J Stu - For clocks and compasses we set the aux value to a special one that signals we should use a default texture
                    // rather than the dynamic one for the player
                    item->setAuxValue(0xFF);
                } else if ((item->getAuxValue() & 0xFF) == 0xFF) {
                    // 4J Stu - If the aux value is set to match any
                    item->setAuxValue(0);
                }
            }
            mcle_custom_draw_slot(region, item, alpha, decorations);
        }
    }

protected:
    struct SlotInfo {
        shared_ptr<ItemInstance> item = nullptr;
        unsigned int alpha = 31;
        bool show = true;
    };

    SlotInfo m_hSlotsInfo[m_iMaxHSlotC];
    SlotInfo m_vSlotsInfo[m_iMaxDisplayedVSlotC];
    SlotInfo m_ingredientsSlotsInfo[m_iIngredients3x3SlotC];
    SlotInfo m_craftingOutputSlotInfo;
    SlotInfo m_ingredientsInfo[4];

    AbstractContainerMenu *m_menu = nullptr;
    int m_iMenuInventoryStart = 0;
    int m_iMenuHotBarStart = 0;
    std::shared_ptr<ItemInstance> m_inventoryItems[CRAFTING_HOTBAR_SLOT_END];

    MCLEUISlotList m_slotListCraftingHSlots;
    MCLEUISlotList m_slotListCrafting1VSlots, m_slotListCrafting2VSlots[2], m_slotListCrafting3VSlots[3];
    MCLEUISlotList m_slotListIngredientsLayout, m_slotListCraftingOutput;
    MCLEUISlotList m_slotListIngredients[4];
    MCLEUISlotList m_slotListInventory, m_slotListHotBar;
    MCLEUILabel m_labelIngredientsDesc[4];
    MCLEUILabel m_labelDescription;
    MCLEUILabel m_labelGroupName, m_labelItemName, m_labelInventory, m_labelIngredients;

    int getPad() override { return 0; }

    void hideAllHSlots() override {
        for (unsigned int iIndex = 0; iIndex < m_iMaxHSlotC; ++iIndex) {
            m_hSlotsInfo[iIndex].item = nullptr;
            m_hSlotsInfo[iIndex].alpha = 31;
            m_hSlotsInfo[iIndex].show = false;
        }
    }

    void hideAllVSlots() override {
        for (unsigned int iIndex = 0; iIndex < m_iMaxDisplayedVSlotC; ++iIndex) {
            m_vSlotsInfo[iIndex].item = nullptr;
            m_vSlotsInfo[iIndex].alpha = 31;
            m_vSlotsInfo[iIndex].show = false;
        }
    }

    void hideAllIngredientsSlots() override {
        for (int i = 0; i < m_iIngredientsC; i++) {
            m_ingredientsInfo[i].item = nullptr;
            m_ingredientsInfo[i].alpha = 31;
            m_ingredientsInfo[i].show = false;

            m_labelIngredientsDesc[i].setLabel(L"");

            mcle_ui_call("", "ShowIngredient", {mcle_ui_num(i), mcle_ui_bool(false)});
        }
    }

    void setCraftHSlotItem(int iPad, int iIndex, shared_ptr<ItemInstance> item, unsigned int uiAlpha) override {
        m_hSlotsInfo[iIndex].item = item;
        m_hSlotsInfo[iIndex].alpha = uiAlpha;
        m_hSlotsInfo[iIndex].show = true;
    }

    void setCraftVSlotItem(int iPad, int iIndex, shared_ptr<ItemInstance> item, unsigned int uiAlpha) override {
        m_vSlotsInfo[iIndex].item = item;
        m_vSlotsInfo[iIndex].alpha = uiAlpha;
        m_vSlotsInfo[iIndex].show = true;
    }

    void setCraftingOutputSlotItem(int iPad, shared_ptr<ItemInstance> item) override {
        m_craftingOutputSlotInfo.item = item;
        m_craftingOutputSlotInfo.alpha = 31;
        m_craftingOutputSlotInfo.show = item != nullptr;
    }

    void setCraftingOutputSlotRedBox(bool show) override { m_slotListCraftingOutput.showSlotRedBox(0, show); }

    void setIngredientSlotItem(int iPad, int index, shared_ptr<ItemInstance> item) override {
        m_ingredientsSlotsInfo[index].item = item;
        m_ingredientsSlotsInfo[index].alpha = 31;
        m_ingredientsSlotsInfo[index].show = item != nullptr;
    }

    void setIngredientSlotRedBox(int index, bool show) override { m_slotListIngredientsLayout.showSlotRedBox(index, show); }

    void setIngredientDescriptionItem(int iPad, int index, shared_ptr<ItemInstance> item) override {
        m_ingredientsInfo[index].item = item;
        m_ingredientsInfo[index].alpha = 31;
        m_ingredientsInfo[index].show = item != nullptr;

        mcle_ui_call("", "ShowIngredient", {mcle_ui_num(index), mcle_ui_bool(m_ingredientsInfo[index].show)});
    }

    void setIngredientDescriptionRedBox(int index, bool show) override {
        m_slotListIngredients[index].showSlotRedBox(0, show);
    }

    void setIngredientDescriptionText(int index, LPCWSTR text) override {
        m_labelIngredientsDesc[index].setLabel(text ? text : L"");
    }

    void setShowCraftHSlot(int iIndex, bool show) override { m_hSlotsInfo[iIndex].show = show; }

    void showTabHighlight(int iIndex, bool show) override {
        if (show) mcle_ui_call("", "SetActiveTab", {mcle_ui_num(iIndex)});
    }

    void setGroupText(LPCWSTR text) override { m_labelGroupName.setLabel(text ? text : L""); }
    void setDescriptionText(LPCWSTR text) override { m_labelDescription.setLabel(text ? text : L""); }
    void setItemText(LPCWSTR text) override { m_labelItemName.setLabel(text ? text : L""); }

    // handled differently
    void scrollDescriptionUp() override {}
    void scrollDescriptionDown() override {}

    void updateHighlightAndScrollPositions() override {
        int selectorType = 0;
        if (CanBeMadeA[m_iCurrentSlotHIndex].iCount == 2) {
            selectorType = 1;
        } else if (CanBeMadeA[m_iCurrentSlotHIndex].iCount > 2) {
            selectorType = 2;
        }
        mcle_ui_call("", "MoveSelector", {mcle_ui_num(m_iCurrentSlotHIndex), mcle_ui_num(selectorType)});
        mcle_ui_call("", "SelectVerticalItem", {mcle_ui_num(m_iCurrentSlotVIndex)});
    }

    // Not needed
    void updateVSlotPositions(int iSlots, int i) override {}

    void UpdateMultiPanel() override {
        mcle_ui_call("", "showPanelDisplay", {mcle_ui_num(m_iDisplayDescription)});
    }
};

} // namespace

// Minecraft::tick's crafting action opens the 2x2, and LocalPlayer::
// startCrafting the 3x3 for the server's ContainerOpenPacket. Both play the
// press, the 3x3 once it's open.
bool mcle_container_open_crafting(bool table) {
    if (mcle_container_is_open()) return false;
    std::lock_guard<std::mutex> lock(mcle_sim_tick_mutex());
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player) return false;
    if (table) {
        CraftingMenu *menu = dynamic_cast<CraftingMenu *>(player->containerMenu);
        if (!menu || menu->containerId != mcle_container_open_id()) return false;
        // ServerPlayer::startCrafting listens to the menu so slotChanged
        // can send the client every slot that changes. The client is this
        // player, like the inventory menu, which never gets the listener.
        menu->removeSlotListener(player.get());
    }
    mcle_container_prepare();
    mcleContainerUI.PlayUISFX(eSFX_Press);
    mcle_game_set_menu_displayed(true);
    mcle_container_begin(new CraftingScene(table, player));
    return true;
}
