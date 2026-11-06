// IUIScene_HUD::updateFrameTick and renderPlayerHealth, the part that reads
// the player. The scene side (the SetX / ShowX calls into HUD1080.swf) is
// in MinecraftViewController.mm.

#include "MCLEHud.h"
#include "MCLEPlayerInput.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/ServerPlayerGameMode.h"
#include "../../../upstream/Minecraft.World/MobEffect.h"
#include "../../../upstream/Minecraft.World/Material.h"
#include "../../../upstream/Minecraft.World/FoodData.h"
#include "../../../upstream/Minecraft.World/Inventory.h"
#include "../../../upstream/Minecraft.World/ItemInstance.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/LivingEntity.h"
#include "../../../upstream/Minecraft.Client/BossMobGuiInfo.h"

namespace {
std::mutex g_mutex;
MCLEHudState g_state = {};
// UIScene_HUD::m_uiSelectedItemOpacityCountDown
unsigned int g_selectedItemOpacityCountDown = 0;
int g_labelCalls = 0, g_hideLabelCalls = 0;
char g_label[128] = "";
// CMinecraftApp::m_uiOpacityCountDown
unsigned int g_opacityCountDown = 0;
// The hotbar the render thread draws, copied so it never touches the
// inventory the sim thread changes
std::shared_ptr<ItemInstance> g_hotbar[9];
}

void mcle_hud_hotbar(std::shared_ptr<ItemInstance> out[9]) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < 9; i++) out[i] = g_hotbar[i];
}

void mcle_hud_reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    // The label counters carry on so the scene doesn't replay an old label
    g_state.valid = 0;
    g_label[0] = 0;
    g_selectedItemOpacityCountDown = 0;
    g_opacityCountDown = 0;
    for (int i = 0; i < 9; i++) g_hotbar[i].reset();
}

void mcle_hud_scrolled() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_opacityCountDown = 120; // 6 seconds
}

static std::string to_utf8(const std::wstring &w) {
    std::string s;
    for (wchar_t wc : w) {
        unsigned int c = (unsigned int)wc;
        if (c < 0x80) {
            s += (char)c;
        } else if (c < 0x800) {
            s += (char)(0xC0 | (c >> 6));
            s += (char)(0x80 | (c & 0x3F));
        } else {
            s += (char)(0xE0 | ((c >> 12) & 0x0F));
            s += (char)(0x80 | ((c >> 6) & 0x3F));
            s += (char)(0x80 | (c & 0x3F));
        }
    }
    return s;
}

int mcle_hud_boss(float *health, char *name, int cap) {
    // Is boss present?
    bool noBoss = BossMobGuiInfo::name.empty() || BossMobGuiInfo::displayTicks <= 0;
    if (noBoss) return 0;
    BossMobGuiInfo::displayTicks--;
    *health = BossMobGuiInfo::healthProgress;
    snprintf(name, cap, "%s", to_utf8(BossMobGuiInfo::name).c_str());
    return 1;
}

// UIScene_HUD::SetSelectedLabel
void mcle_hud_set_selected_label(const std::wstring &label) {
    std::string text = to_utf8(label);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!label.empty()) g_selectedItemOpacityCountDown = SharedConstants::TICKS_PER_SECOND * 3;
    snprintf(g_label, sizeof(g_label), "%s", text.c_str());
    g_labelCalls++;
}

void mcle_hud_update(const std::shared_ptr<ServerPlayer> &player) {
    if (!player || !player->gameMode) return;
    MCLEHudState st = {};
    st.valid = 1;
    st.activeSlot = player->inventory->selected;
    // MultiPlayerGameMode::canHurtPlayer / hasExperience
    st.canHurtPlayer = player->gameMode->isSurvival();
    st.mayfly = player->abilities.mayfly;
    st.hasExperience = player->gameMode->isSurvival();

    // renderPlayerHealth
    st.regen = player->hasEffect(MobEffect::regeneration);
    // Entity's, the hit flash. ServerPlayer has its own for spawn protection.
    int invulnerableTime = player->Entity::invulnerableTime;
    bool blink = invulnerableTime / 3 % 2 == 1;
    if (invulnerableTime < 10) blink = false;
    st.blink = blink;
    st.health = static_cast<int>(ceil(player->getHealth()));
    // The client player's lastHealth. Only LivingEntity::hurt sets it, which
    // MultiplayerLocalPlayer::hurt never calls, so it stays 0. The server
    // player's is its health before the last hit, which kept the hearts up.
    st.lastHealth = 0;
    st.poison = player->hasEffect(MobEffect::poison);
    st.wither = player->hasEffect(MobEffect::wither);
    st.absorption = player->getAbsorptionAmount();
    st.armour = player->getArmorValue();

    FoodData *foodData = player->getFoodData();
    st.food = foodData->getFoodLevel();
    st.lastFood = foodData->getLastFoodLevel();
    st.hunger = player->hasEffect(MobEffect::hunger);
    st.saturation = static_cast<int>(foodData->getSaturationLevel());

    st.underWater = player->isUnderLiquid(Material::water);
    if (st.underWater) {
        int count = static_cast<int>(ceil((player->getAirSupply() - 2) * 10.0f / Player::TOTAL_AIR_SUPPLY));
        int extra = static_cast<int>(ceil((player->getAirSupply()) * 10.0f / Player::TOTAL_AIR_SUPPLY)) - count;
        st.air = count;
        st.airExtra = extra;
    }

    // InventoryMenu::USE_ROW_SLOT_START + slot is inventory slot `slot`
    std::shared_ptr<ItemInstance> hotbar[9];
    for (int i = 0; i < 9; i++) {
        std::shared_ptr<ItemInstance> item = player->inventory->items[i];
        if (item != nullptr) {
            hotbar[i] = item->copy();
            hotbar[i]->popTime = item->popTime;
        }
    }

    // LocalPlayer::isRidingJumpable
    std::shared_ptr<Entity> riding = player->riding;
    if (riding != nullptr) {
        if (riding->instanceof(eTYPE_LIVINGENTITY)) {
            std::shared_ptr<LivingEntity> living = std::dynamic_pointer_cast<LivingEntity>(riding);
            st.riding = 2;
            st.riderHealth = static_cast<int>(ceil(living->getHealth()));
            st.riderMaxHealth = static_cast<int>(living->getMaxHealth());
        } else {
            st.riding = 1;
        }
        st.ridingJumpable = riding->GetType() == eTYPE_HORSE;
    }
    st.jumpRidingScale = mcle_player_jump_riding_scale();
    snprintf(st.displayName, sizeof(st.displayName), "%s", to_utf8(ProfileManager.GetDisplayName(0)).c_str());

    st.expProgress = player->experienceProgress;
    st.expLevel = player->experienceLevel;
    st.xpNeeded = player->getXpNeededForNextLevel();

    std::lock_guard<std::mutex> lock(g_mutex);
    // Minecraft::tick: TickOpacityTimer
    if (g_opacityCountDown > 0) g_opacityCountDown--;
    st.opacityTimer = static_cast<int>(g_opacityCountDown);
    for (int i = 0; i < 9; i++) g_hotbar[i] = hotbar[i];
    // updateFrameTick: the label fades out in the movie a second before the
    // countdown would end
    if (g_selectedItemOpacityCountDown > 0) {
        --g_selectedItemOpacityCountDown;
        if (g_selectedItemOpacityCountDown < SharedConstants::TICKS_PER_SECOND * 1) {
            g_hideLabelCalls++;
            g_selectedItemOpacityCountDown = 0;
        }
    }
    st.labelCalls = g_labelCalls;
    st.hideLabelCalls = g_hideLabelCalls;
    memcpy(st.label, g_label, sizeof(st.label));
    g_state = st;
}

extern "C" void mcle_hud_get_state(MCLEHudState *out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    *out = g_state;
}
