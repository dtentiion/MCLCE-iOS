// IUIScene_HUD::updateFrameTick and renderPlayerHealth, the part that reads
// the player. The scene side (the SetX / ShowX calls into HUD1080.swf) is
// in MinecraftViewController.mm.

#include "MCLEHud.h"

#include <cmath>
#include <mutex>

#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/ServerPlayerGameMode.h"
#include "../../../upstream/Minecraft.World/MobEffect.h"
#include "../../../upstream/Minecraft.World/Material.h"
#include "../../../upstream/Minecraft.World/FoodData.h"
#include "../../../upstream/Minecraft.World/Inventory.h"

namespace {
std::mutex g_mutex;
MCLEHudState g_state = {};
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
    g_state = st;
}

extern "C" void mcle_hud_get_state(MCLEHudState *out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    *out = g_state;
}
