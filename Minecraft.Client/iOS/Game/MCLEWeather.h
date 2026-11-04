#pragma once

#include <memory>

class LivingEntity;
class Level;

// GameRenderer::tickRain then the _tick++ at the end of GameRenderer::tick.
// Once a game tick on the sim thread.
void mcle_weather_tick(const std::shared_ptr<LivingEntity> &player, Level *level);

// GameRenderer::renderSnowAndRain, on the render thread after the chunks
void mcle_weather_render(const std::shared_ptr<LivingEntity> &player, Level *level, float a);
