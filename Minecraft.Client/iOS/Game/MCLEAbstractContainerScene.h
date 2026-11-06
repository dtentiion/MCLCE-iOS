#pragma once

// UIScene_AbstractContainerMenu, the Iggy side every pointer-and-slots
// container menu shares. IUIScene_AbstractContainerMenu's body is compiled
// in MCLEContainerMenu.cpp; the menus derive from this.

#include <cstdio>
#include <cstring>

#include "MCLEContainerScene.h"

#include "../../../upstream/Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../upstream/Minecraft.World/HtmlString.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/Common/UI/IUIScene_AbstractContainerMenu.h"

class MCLEAbstractContainerScene : public MCLEContainerScene, public virtual IUIScene_AbstractContainerMenu {
public:
    MCLEAbstractContainerScene()
        : m_slotListHotbar("MainPanel.hotbarList"), m_slotListInventory("MainPanel.inventoryList"),
          m_labelInventory("MainPanel.inventoryLabel"), m_cursorPath("MainPanel.cursor") {}

    int getPad() override { return 0; }
    int getMovieWidth() override { return 1920; }
    int getMovieHeight() override { return 1080; }
    void getMouseToSWFScale(float &scaleX, float &scaleY) override {
        scaleX = 1.0f;
        scaleY = 1.0f;
    }

    void handleInput(int key, bool repeat, bool pressed, bool released) override {
        if (m_bIgnoreInput) return;
        if (pressed) handleKeyDown(getPad(), key, repeat);
    }

    bool getIgnoreInput() { return m_bIgnoreInput; }
    void setIgnoreInput(bool bVal) { m_bIgnoreInput = bVal; }

    // UIScene::tick runs the timers and sends any label that changed, then
    // the pointer moves
    void tick(double now) override {
        tickTimers(now);
        m_labelInventory.tick();
        m_cursorPath.tick();
        onMouseTick();
    }

    bool pointer(float *x, float *y) override {
        *x = m_pointerPos.x;
        *y = m_pointerPos.y;
        return true;
    }

    // There's no tutorial state to put back. The client's closeContainer()
    // ends up in the server's handleContainerClose, which is just
    // doCloseContainer(). ServerPlayer's own closeContainer() would send a
    // ContainerClosePacket.
    void handleDestroy() override {
        std::shared_ptr<ServerPlayer> player = mcle_local_player();
        if (player != nullptr && player->containerMenu != nullptr && m_menu != nullptr &&
            player->containerMenu->containerId == m_menu->containerId) {
            player->doCloseContainer();
        }
    }

    // What the slots held at the end of the frame, for drawing
    void snapshot() override {
        m_slotItems.clear();
        m_slotValid.clear();
        m_carried = nullptr;
        if (!m_menu) return;
        unsigned int count = m_menu->getSize();
        for (unsigned int i = 0; i < count; ++i) {
            Slot *slot = m_menu->getSlot(i);
            std::shared_ptr<ItemInstance> item = slot ? slot->getItem() : nullptr;
            m_slotItems.push_back(copyOf(item));
            m_slotValid.push_back(item == nullptr || m_menu->isValidIngredient(item, i));
        }
        std::shared_ptr<ServerPlayer> player = mcle_local_player();
        if (player) {
            std::shared_ptr<ItemInstance> carried = player->inventory->getCarried();
            m_carried = copyOf(carried);
            m_carriedValid = carried == nullptr || m_menu->isValidIngredient(carried, -1);
        }
    }

    // UIScene_AbstractContainerMenu::customDraw
    void customDraw(const MCLECustomDrawRegion &region) override {
        std::shared_ptr<ItemInstance> item = nullptr;
        bool valid = true;
        int slotId = -1;
        if (strcmp(region.name, "pointerIcon") == 0) {
            item = m_carried;
            valid = m_carriedValid;
        } else if (sscanf(region.name, "slot_%d", &slotId) == 1 && slotId >= 0 &&
                   slotId < static_cast<int>(m_slotItems.size())) {
            item = m_slotItems[slotId];
            valid = m_slotValid[slotId];
        }
        if (item != nullptr) mcle_custom_draw_slot(region, item, valid ? 1.0f : 0.5f, true);
    }

    MCLEControlRect m_backgroundPanel{};

protected:
    MCLEUISlotList m_slotListHotbar, m_slotListInventory;
    MCLEUILabel m_labelInventory, m_cursorPath;
    ESceneSection m_focusSection = eSectionNone;
    bool m_bIgnoreInput = false;

    std::vector<std::shared_ptr<ItemInstance>> m_slotItems;
    std::vector<bool> m_slotValid;
    std::shared_ptr<ItemInstance> m_carried;
    bool m_carriedValid = true;

    static std::shared_ptr<ItemInstance> copyOf(const std::shared_ptr<ItemInstance> &item) {
        return item != nullptr ? item->copy() : nullptr;
    }

    virtual void tickTimers(double now) {}
    virtual MCLEUISlotList *getSection(ESceneSection eSection) { return nullptr; }

    void InitDataAssociations(int iPad, AbstractContainerMenu *menu, int startIndex) override {}

    void PlatformInitialize(int iPad, int startIndex) override {
        m_labelInventory.init(app.GetString(IDS_INVENTORY));

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
        MCLEUISlotList *newFocus = getSection(eSection);
        if (newFocus) newFocus->setFocus(true);

        if (m_focusSection != eSectionNone) {
            MCLEUISlotList *currentFocus = getSection(m_focusSection);
            if (currentFocus != newFocus && currentFocus) currentFocus->setFocus(false);
        }
        m_focusSection = eSection;
    }

    void setFocusToPointer(int iPad) override {
        if (m_focusSection != eSectionNone) {
            MCLEUISlotList *currentFocus = getSection(m_focusSection);
            if (currentFocus) currentFocus->setFocus(false);
        }
        m_focusSection = eSectionNone;
    }

    void SetPointerText(vector<HtmlString> *description, bool newSlot) override {
        m_cursorPath.setLabel(HtmlString::Compose(description), false, newSlot);
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
