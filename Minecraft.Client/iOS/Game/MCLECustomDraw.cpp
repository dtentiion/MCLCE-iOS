// Iggy custom draw for the HUD: UIScene_HUD::customDraw, with
// UIScene::_customDrawSlotControl and UIController's custom draw setup.
// Iggy calls back while drawing the movie; Ruffle can't, so the regions are
// read from it after it draws and the items go on the UI layer above it.

#include "MCLEHud.h"
#include "MCLETutorialPopup.h"

#include <cstdio>
#include <memory>

#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/ItemRenderer.h"
#include "../../../upstream/Minecraft.Client/Lighting.h"
#include "../../../upstream/Minecraft.Client/Textures.h"
#include "../../../upstream/Minecraft.World/Inventory.h"
#include "../../../upstream/Minecraft.World/ItemInstance.h"

extern "C" unsigned char mcle_settings_get(int setting); // MCLE_iOS_Settings.mm
extern "C" int  mcle_metal_ui_frame_begin(void);         // MetalContext.mm
extern "C" void mcle_metal_ui_frame_end(void);
extern "C" void mcle_metal_ui_size(int *out_width, int *out_height);
extern "C" void mcle_glbridge_metal_ortho(float left, float right, float bottom, float top, float zNear, float zFar);

namespace {

const int kSettingInterfaceOpacity = 17; // MCLE_SETTING_InterfaceOpacity

ItemRenderer *g_itemRenderer = nullptr; // UIScene::m_pItemRenderer
bool g_drewLastFrame = false;

// UIScene::_customDrawSlotControl, with the region already in pixels
void drawSlot(float x, float y, float bwidth, float bheight, std::shared_ptr<ItemInstance> item, float fAlpha,
              bool isFoil, bool bDecorations) {
    Minecraft *pMinecraft = Minecraft::GetInstance();

    // Base scale on height of this control, compared to height of what the
    // item renderer normally renders (16 pixels high)
    float scaleX = bwidth / 16.0f;
    float scaleY = bheight / 16.0f;

    glEnable(GL_RESCALE_NORMAL);
    glPushMatrix();
    glRotatef(120, 1, 0, 0);
    Lighting::turnOn();
    glPopMatrix();

    float pop = item->popTime;
    if (pop > 0) {
        glPushMatrix();
        float squeeze = 1 + pop / static_cast<float>(Inventory::POP_TIME_DURATION);
        float sx = x;
        float sy = y;
        float sxoffs = 8 * scaleX;
        float syoffs = 12 * scaleY;
        glTranslatef((float)(sx + sxoffs), (float)(sy + syoffs), 0);
        glScalef(1 / squeeze, (squeeze + 1) / 2, 1);
        glTranslatef((float)-(sx + sxoffs), (float)-(sy + syoffs), 0);
    }

    if (g_itemRenderer == nullptr) g_itemRenderer = new ItemRenderer();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_itemRenderer->renderAndDecorateItem(pMinecraft->font, pMinecraft->textures, item, x, y, scaleX, scaleY, fAlpha,
                                          isFoil, false, true);

    if (pop > 0) {
        glPopMatrix();
    }

    if (bDecorations) {
        if ((scaleX != 1.0f) || (scaleY != 1.0f)) {
            glPushMatrix();
            glScalef(scaleX, scaleY, 1.0f);
            int iX = static_cast<int>(0.5f + ((float)x) / scaleX);
            int iY = static_cast<int>(0.5f + ((float)y) / scaleY);

            g_itemRenderer->renderGuiItemDecorations(pMinecraft->font, pMinecraft->textures, item, iX, iY, fAlpha);
            glPopMatrix();
        } else {
            g_itemRenderer->renderGuiItemDecorations(pMinecraft->font, pMinecraft->textures, item,
                                                     static_cast<int>(x), static_cast<int>(y), fAlpha);
        }
    }

    Lighting::turnOff();
    glDisable(GL_RESCALE_NORMAL);
}

// The HUD's opacity for the item, same as the scene's own
float slotAlpha(int opacityTimer) {
    unsigned char ucAlpha = mcle_settings_get(kSettingInterfaceOpacity);
    float fVal;
    if (ucAlpha < 80) {
        // is the opacity timer running
        if (opacityTimer != 0) {
            if (opacityTimer < 10) {
                float fStep = (80.0f - static_cast<float>(ucAlpha)) / 10.0f;
                fVal = 0.01f * (80.0f - ((10.0f - static_cast<float>(opacityTimer)) * fStep));
            } else {
                fVal = 0.01f * 80.0f;
            }
        } else {
            fVal = 0.01f * static_cast<float>(ucAlpha);
        }
    } else {
        fVal = 0.01f * static_cast<float>(ucAlpha);
    }
    return fVal;
}

// UIController::setupCustomDrawGameState
void setupCustomDrawGameState() {
    int w = 0, h = 0;
    mcle_metal_ui_size(&w, &h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    mcle_glbridge_metal_ortho(0, static_cast<float>(w), static_cast<float>(h), 0, 1000, 3000);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(true);
}

// UIController::endCustomDrawGameState
void endCustomDrawGameState() {
    glDepthMask(false);
    glDisable(GL_ALPHA_TEST);
}

} // namespace

// The UI layer for another scene's custom draw, false when it can't be drawn
bool mcle_custom_draw_begin() {
    Minecraft *pMinecraft = Minecraft::GetInstance();
    if (!pMinecraft || !pMinecraft->textures || !pMinecraft->font) return false;
    if (mcle_metal_ui_frame_begin() != 0) return false;
    // so the HUD clears it once it's back
    g_drewLastFrame = true;
    setupCustomDrawGameState();
    return true;
}

// UIScene::customDrawSlotControl for one region, already in pixels
void mcle_custom_draw_slot(const MCLECustomDrawRegion &region, std::shared_ptr<ItemInstance> item, float alpha,
                           bool decorations) {
    if (item == nullptr) return;
    glLoadIdentity();
    glTranslatef(0, 0, -2000);
    try {
        drawSlot(region.x0, region.y0, region.x1 - region.x0, region.y1 - region.y0, item, alpha, item->isFoil(),
                 decorations);
    } catch (...) {
    }
}

void mcle_custom_draw_end() {
    endCustomDrawGameState();
    mcle_metal_ui_frame_end();
}

extern "C" void mcle_hud_custom_draw(const MCLECustomDrawRegion *regions, int count) {
    Minecraft *pMinecraft = Minecraft::GetInstance();
    if (!pMinecraft || !pMinecraft->textures || !pMinecraft->font) return;

    std::shared_ptr<ItemInstance> hotbar[9];
    mcle_hud_hotbar(hotbar);
    bool anything = false;
    for (int i = 0; i < count; i++) {
        int slot = -1;
        if (sscanf(regions[i].name, "slot_%d", &slot) == 1 && slot >= 0 && slot < 9 && hotbar[slot] != nullptr) {
            anything = true;
        }
    }
    // The tutorial popup's item over the HUD
    const bool popupIcon = count > 0 && mcle_tutorial_popup_has_icon();
    // An empty layer only needs clearing once
    if (!anything && !popupIcon && !g_drewLastFrame) return;
    if (mcle_metal_ui_frame_begin() != 0) return;
    g_drewLastFrame = anything || popupIcon;

    if (anything || popupIcon) {
        MCLEHudState st;
        mcle_hud_get_state(&st);
        float fVal = slotAlpha(st.opacityTimer);

        setupCustomDrawGameState();
        for (int i = 0; anything && i < count; i++) {
            // UIScene_HUD::customDraw
            int slot = -1;
            if (sscanf(regions[i].name, "slot_%d", &slot) != 1 || slot < 0 || slot >= 9) continue;
            mcle_custom_draw_slot(regions[i], hotbar[slot], fVal, true);
        }
        if (popupIcon) mcle_tutorial_popup_custom_draw();
        endCustomDrawGameState();
    }
    mcle_metal_ui_frame_end();
}
