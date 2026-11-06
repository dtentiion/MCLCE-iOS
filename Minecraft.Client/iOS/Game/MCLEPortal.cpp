// LocalPlayer::aiStep's portal time. The client's player is the server's
// ServerPlayer here, whose Entity::baseTick counts the server's portal time
// for the level change. The client's own one fades the portal overlay
// (Gui::renderTp) and the view skew (GameRenderer::setupCamera) in, and
// plays the trigger sound as it starts.

#include "MCLEPortal.h"

#include <atomic>

#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.World/MobEffect.h"
#include "../../../upstream/Minecraft.World/MobEffectInstance.h"
#include "../../../upstream/Minecraft.World/Random.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/SoundTypes.h"

std::shared_ptr<ServerPlayer> mcle_local_player();   // MCLEGameLoop.cpp

namespace {
// LocalPlayer::isInsidePortal, set by the client player's handleInsidePortal.
// The client player is made again with each level change, so its
// changingDimensionDelay never gets set and only the tile counts.
bool g_insidePortal = false;
std::atomic<float> g_portalTime{0.0f};
std::atomic<float> g_oPortalTime{0.0f};
// The client player's own random
Random g_random;
} // namespace

// PortalTile::entityInside for the client's player
void mcle_client_entity_inside_portal(shared_ptr<Entity> entity) {
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    if (!player || entity.get() != player.get()) return;
    if (entity->riding == nullptr && entity->rider.lock() == nullptr) g_insidePortal = true;
}

// LocalPlayer::aiStep, the part before the input. The client level is client
// side, so riding isn't stopped, and minecraft->screen is never set.
void mcle_portal_client_tick(const std::shared_ptr<ServerPlayer> &player) {
    float portalTime = g_portalTime.load(std::memory_order_relaxed);
    g_oPortalTime.store(portalTime, std::memory_order_relaxed);
    if (g_insidePortal) {
        if (portalTime == 0) {
            Minecraft::GetInstance()->soundEngine->playUI(eSoundType_PORTAL_TRIGGER, 1, g_random.nextFloat() * 0.4f + 0.8f);
        }
        portalTime += 1 / 80.0f;
        if (portalTime >= 1) {
            portalTime = 1;
        }
        g_insidePortal = false;
    } else if (player->hasEffect(MobEffect::confusion) &&
               player->getEffect(MobEffect::confusion)->getDuration() > (SharedConstants::TICKS_PER_SECOND * 3)) {
        portalTime += 1 / 150.0f;
        if (portalTime > 1) {
            portalTime = 1;
        }
    } else {
        if (portalTime > 0) portalTime -= 1 / 20.0f;
        if (portalTime < 0) portalTime = 0;
    }
    g_portalTime.store(portalTime, std::memory_order_relaxed);
}

// oPortalTime + (portalTime - oPortalTime) * a
float mcle_portal_client_time(float a) {
    const float oPortalTime = g_oPortalTime.load(std::memory_order_relaxed);
    return oPortalTime + (g_portalTime.load(std::memory_order_relaxed) - oPortalTime) * a;
}

// LocalPlayer's constructor
void mcle_portal_client_reset() {
    g_insidePortal = false;
    g_portalTime.store(0.0f, std::memory_order_relaxed);
    g_oPortalTime.store(0.0f, std::memory_order_relaxed);
}
