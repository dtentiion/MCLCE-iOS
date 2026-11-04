#pragma once

#include <memory>

class ServerPlayer;
class ServerLevel;

// Breaking, placing, attacking, using items and hotbar scrolling for the
// local player. Call once per 20Hz game tick with the pad's
// _360_JOY_BUTTON_* bits.
void mcle_player_input_tick(const std::shared_ptr<ServerPlayer> &player, ServerLevel *level,
                            unsigned int buttons);

// Input::tick plus the start of LocalPlayer::aiStep: walking input, sneak
// toggle, sprint and creative flying. Stick values are -1..1, x right and
// y forward. Call once per game tick before the player's doTick.
void mcle_player_move_tick(const std::shared_ptr<ServerPlayer> &player, float stickX, float stickY,
                           unsigned int buttons);
