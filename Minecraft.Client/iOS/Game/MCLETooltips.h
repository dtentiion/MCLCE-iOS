#pragma once

#ifdef __cplusplus
#include <memory>
class ServerPlayer;
class HitResult;

// Minecraft::tick's tooltips for what the player is looking at and holding.
// Once a game tick on the sim thread, after the pick.
void mcle_tooltips_update(const std::shared_ptr<ServerPlayer> &player, HitResult *hitResult);

extern "C" {
#endif

// The IDS_* string ids last passed to ui.SetTooltips, in the order A, B, X,
// Y, LT, RT, LB, RB, LS, RS. -1 is no tooltip.
void mcle_tooltips_get(int out[10]);

#ifdef __cplusplus
}
#endif
