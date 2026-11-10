// What the local player's client does that the server here already doesn't:
// ClientConnection::handleGameEvent for the packets the server sends it, and
// which level is the client's, where the server's own entities and tile
// entities also do their client side (scripts/patch-client-effects.py).

#include <memory>
#include <string>

#include "../../../upstream/Minecraft.World/GameEventPacket.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/SoundTypes.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"

std::shared_ptr<ServerPlayer> mcle_local_player();   // MCLEGameLoop.cpp
void mcle_local_player_win_game();   // MCLEGameLoop.cpp
// MCLEGameLoop.cpp, with the Gui's lock held
void mcle_gui_add_message(const std::wstring &message, bool bIsDeathMessage);
void mcle_client_weather_set_raining(bool raining);   // MCLEWeather.cpp

// Whether a level is the one the local player is in, the client's level
bool mcle_client_level(Level *level) {
    std::shared_ptr<ServerPlayer> player = mcle_local_player();
    return level != nullptr && player != nullptr && player->level == level;
}

// ClientConnection::handleGameEvent
void mcle_client_game_event(std::shared_ptr<GameEventPacket> packet) {
    int event = packet->_event;
    if (event >= 0 && event < GameEventPacket::EVENT_LANGUAGE_ID_LENGTH) {
        if (GameEventPacket::EVENT_LANGUAGE_ID[event] > 0) {
            // LocalPlayer::displayClientMessage, Gui::displayClientMessage
            mcle_gui_add_message(app.GetString(GameEventPacket::EVENT_LANGUAGE_ID[event]), false);
        }
    }
    // CHANGE_GAME_MODE sets the client's game mode, which is the server's
    // here and already has it. START_SAVING and STOP_SAVING do nothing on
    // the host.
    if (event == GameEventPacket::START_RAINING) {
        mcle_client_weather_set_raining(true);
    } else if (event == GameEventPacket::STOP_RAINING) {
        mcle_client_weather_set_raining(false);
    } else if (event == GameEventPacket::WIN_GAME) {
        mcle_local_player_win_game();
    } else if (event == GameEventPacket::SUCCESSFUL_BOW_HIT) {
        std::shared_ptr<ServerPlayer> player = mcle_local_player();
        if (player == nullptr || player->level == nullptr) return;
        // The client player's y is at its eyes, 1.62 up, and its
        // getHeadHeight is Player's 0.12
        player->level->playLocalSound(player->x, player->y + 1.62f - player->heightOffset + 0.12f, player->z,
                                      eSoundType_RANDOM_BOW_HIT, 0.18f, 0.45f, false);
    }
}
