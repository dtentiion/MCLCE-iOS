#pragma once

// The players' privileges and the host's in game options as the server and
// the local player's client handle them. The local player is the server's
// own and has no client connection, so the PlayerInfoPacket and
// ServerSettingsChangedPacket the server sends it come here.

#include <memory>

class PlayerInfoPacket;
class ServerPlayer;
class ServerSettingsChangedPacket;

// ClientConnection::handleLogin: the privileges the LoginPacket gives the
// player as it joins
void mcle_client_login_privileges(const std::shared_ptr<ServerPlayer> &player);

// ClientConnection::handlePlayerInfo
void mcle_client_player_info(std::shared_ptr<PlayerInfoPacket> packet);

// ClientConnection::handleServerSettingsChanged
void mcle_client_server_settings_changed(std::shared_ptr<ServerSettingsChangedPacket> packet);

// PlayerConnection::handleServerSettingsChanged for the host's packet, with
// the sim tick lock held
void mcle_server_settings_changed(std::shared_ptr<ServerSettingsChangedPacket> packet);
