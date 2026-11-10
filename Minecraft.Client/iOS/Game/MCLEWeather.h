#pragma once

#include <memory>

class LivingEntity;
class Level;

// GameRenderer::tickRain then the _tick++ at the end of GameRenderer::tick.
// Once a game tick on the sim thread.
void mcle_weather_tick(const std::shared_ptr<LivingEntity> &player, Level *level);

// GameRenderer::renderSnowAndRain, on the render thread after the chunks
void mcle_weather_render(const std::shared_ptr<LivingEntity> &player, Level *level, float a);

// GameRenderer::_tick, for the render thread
int mcle_renderer_tick();

// A client's level has its own rain level: set outright when the server's
// rain starts or stops (ClientConnection::handleGameEvent), stepped toward
// its raining flag every tick (MultiPlayerLevel::tickWeather), and with no
// thunder, which a client is never told about. The one level here is the
// server's, so the client's is kept next to it, and Level::getRainLevel and
// getThunderLevel answer with it while one of these is alive on the thread:
// the frame, and the rain's own tick.
class MCLEClientSide {
public:
    MCLEClientSide();
    ~MCLEClientSide();
    MCLEClientSide(const MCLEClientSide &) = delete;
    MCLEClientSide &operator=(const MCLEClientSide &) = delete;
};

// The level the client is in now. PlayerList::sendLevelInfo tells a player
// arriving in a level whether it's raining there.
void mcle_client_weather_set_level(Level *level);

// GameEventPacket START_RAINING / STOP_RAINING
void mcle_client_weather_set_raining(bool raining);
