#pragma once

// The players' privileges as the local player's client handles them. The
// local player is the server's own and has no client connection, so the
// PlayerInfoPacket the server sends it comes here.

#include <memory>

class PlayerInfoPacket;
class ServerPlayer;

// ClientConnection::handleLogin: the privileges the LoginPacket gives the
// player as it joins
void mcle_client_login_privileges(const std::shared_ptr<ServerPlayer> &player);

// ClientConnection::handlePlayerInfo
void mcle_client_player_info(std::shared_ptr<PlayerInfoPacket> packet);
