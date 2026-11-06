#pragma once

#include <memory>

class ServerPlayer;
class ServerLevel;

// Breaking, placing, attacking, using items and hotbar scrolling for the
// local player. Call once per 20Hz game tick with the pad's
// _360_JOY_BUTTON_* bits.
void mcle_player_input_tick(const std::shared_ptr<ServerPlayer> &player, ServerLevel *level,
                            unsigned int buttons);

// Input::tick plus the start of LocalPlayer::aiStep: walking and looking,
// sneak toggle, sprint and creative flying. Stick values are -1..1, x right
// and y forward / up. Call once per game tick before the player's doTick.
void mcle_player_move_tick(const std::shared_ptr<ServerPlayer> &player, float stickX, float stickY,
                           float lookX, float lookY, unsigned int buttons, float sensitivity,
                           bool invertLook);

// The end of LocalPlayer::aiStep, after Player::aiStep: the flying move.
// Player::aiStep calls it for every player, it only acts on the local one.
class Player;
void mcle_player_ai_step_end(Player *player);

// A SetEntityLinkPacket the server sent the local player: what
// MultiplayerLocalPlayer::ride does when the player gets on or off
void mcle_player_entity_link(int type, int sourceId, int destId);

// LocalPlayer::getJumpRidingScale: how far the horse jump bar is charged
float mcle_player_jump_riding_scale();

// LocalPlayer::ThirdPersonView, cycled with the left stick click
int mcle_third_person_view();

// LocalPlayer's xBob / yBob at partial tick a, for ItemInHandRenderer's sway
void mcle_player_bob(float a, float *xBob, float *yBob);

// GameRenderer::tickFov once a tick, and the smoothed factor getFov uses
void mcle_player_fov_tick(const std::shared_ptr<ServerPlayer> &player);
float mcle_player_fov_factor(float a);

// Minecraft::hitResult from this tick's pick. Sim thread only, it's
// replaced every tick.
class HitResult;
HitResult *mcle_player_hit_result();

// The block the player was targeting at the last tick, if any.
bool mcle_player_hit_tile(int *x, int *y, int *z, int *f);

// Forget the player, its target and the mining state when leaving the world
void mcle_player_input_reset();
