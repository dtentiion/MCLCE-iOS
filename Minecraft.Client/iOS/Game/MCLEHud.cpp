// IUIScene_HUD::updateFrameTick and renderPlayerHealth, the part that reads
// the player. The scene side (the SetX / ShowX calls into HUD1080.swf) is
// in MinecraftViewController.mm.

#include "MCLEHud.h"

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
#include "../../../upstream/Minecraft.World/SharedConstants.h"

namespace {
std::mutex g_mutex;
MCLEHudState g_state = {};
// UIScene_HUD::m_uiSelectedItemOpacityCountDown
unsigned int g_selectedItemOpacityCountDown = 0;
int g_labelCalls = 0, g_hideLabelCalls = 0;
char g_label[128] = "";
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
    st.hasExperience = player->gameMode->isSurvival();

    // renderPlayerHealth
    st.regen = player->hasEffect(MobEffect::regeneration);
    // Entity's, the hit flash. ServerPlayer has its own for spawn protection.
    int invulnerableTime = player->Entity::invulnerableTime;
    bool blink = invulnerableTime / 3 % 2 == 1;
    if (invulnerableTime < 10) blink = false;
    st.blink = blink;
    st.health = static_cast<int>(ceil(player->getHealth()));
    st.lastHealth = static_cast<int>(ceil(player->lastHealth));
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

    st.expProgress = player->experienceProgress;
    st.expLevel = player->experienceLevel;
    st.xpNeeded = player->getXpNeededForNextLevel();

    std::lock_guard<std::mutex> lock(g_mutex);
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
