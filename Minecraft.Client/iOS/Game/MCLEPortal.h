#pragma once

#include <memory>

class ServerPlayer;

// LocalPlayer's portalTime / oPortalTime for the local player (Game/MCLEPortal.cpp)
void mcle_portal_client_tick(const std::shared_ptr<ServerPlayer> &player);
float mcle_portal_client_time(float a);
// A new client player, after a level change, a respawn or a new world
void mcle_portal_client_reset();
