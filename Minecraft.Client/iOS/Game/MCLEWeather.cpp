// GameRenderer::tickRain and renderSnowAndRain. GameRenderer.cpp doesn't
// build on iOS, so its members these use (_tick, rainXa / rainZa,
// rainSoundTime) live here. tickRain runs on the sim thread and the render
// on the render thread, so each has its own Random; both reseed before
// every use, the same numbers come out.

#include "MCLEWeather.h"

#include <atomic>
#include <cmath>
#include <mutex>

#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/LevelRenderer.h"
#include "../../../upstream/Minecraft.Client/Options.h"
#include "../../../upstream/Minecraft.Client/ParticleEngine.h"
#include "../../../upstream/Minecraft.Client/SmokeParticle.h"
#include "../../../upstream/Minecraft.Client/Tesselator.h"
#include "../../../upstream/Minecraft.Client/Textures.h"
#include "../../../upstream/Minecraft.Client/WaterDropParticle.h"
#include "../../../upstream/Minecraft.World/Biome.h"
#include "../../../upstream/Minecraft.World/BiomeSource.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/LivingEntity.h"
#include "../../../upstream/Minecraft.World/Material.h"
#include "../../../upstream/Minecraft.World/Mth.h"
#include "../../../upstream/Minecraft.World/Random.h"
#include "../../../upstream/Minecraft.World/SoundTypes.h"
#include "../../../upstream/Minecraft.World/Tile.h"

// MCLEGameLoop.cpp
std::recursive_mutex &mcle_particle_mutex();

namespace {

std::atomic<int> g_tick{0};
int g_rainSoundTime = 0;
float *g_rainXa = nullptr;
float *g_rainZa = nullptr;

ResourceLocation &rainLocation() {
    static ResourceLocation location(TN_ENVIRONMENT_RAIN);
    return location;
}

ResourceLocation &snowLocation() {
    static ResourceLocation location(TN_ENVIRONMENT_SNOW);
    return location;
}

} // namespace

int mcle_renderer_tick() {
    return g_tick.load(std::memory_order_relaxed);
}

void mcle_weather_tick(const std::shared_ptr<LivingEntity> &player, Level *level) {
    static Random s_random;
    Random *random = &s_random;
    Minecraft *mc = Minecraft::GetInstance();
    int _tick = g_tick.load(std::memory_order_relaxed);

    // tickRain
    do {
        float rainLevel = level->getRainLevel(1);

        if (!mc->options->fancyGraphics) rainLevel /= 2;
        if (rainLevel == 0) break;

        rainLevel /= (mc->levelRenderer->activePlayers() + 1);

        random->setSeed(_tick * 312987231l);

        int x0 = Mth::floor(player->x);
        int y0 = Mth::floor(player->y);
        int z0 = Mth::floor(player->z);

        int r = 10;

        double rainPosX = 0;
        double rainPosY = 0;
        double rainPosZ = 0;
        int rainPosSamples = 0;

        int rainCount = static_cast<int>(100 * rainLevel * rainLevel);
        if (mc->options->particles == 1) {
            rainCount >>= 1;
        } else if (mc->options->particles == 2) {
            rainCount = 0;
        }
        for (int i = 0; i < rainCount; i++) {
            int x = x0 + random->nextInt(r) - random->nextInt(r);
            int z = z0 + random->nextInt(r) - random->nextInt(r);
            int y = level->getTopRainBlock(x, z);
            int t = level->getTile(x, y - 1, z);
            Biome *biome = level->getBiome(x, z);
            if (y <= y0 + r && y >= y0 - r && biome->hasRain() && biome->getTemperature() >= 0.2f) {
                float xa = random->nextFloat();
                float za = random->nextFloat();
                if (t > 0) {
                    std::lock_guard<std::recursive_mutex> lock(mcle_particle_mutex());
                    if (Tile::tiles[t]->material == Material::lava) {
                        mc->particleEngine->add(std::make_shared<SmokeParticle>(
                            level, x + xa, y + 0.1f - Tile::tiles[t]->getShapeY0(), z + za, 0, 0, 0));
                    } else {
                        if (random->nextInt(++rainPosSamples) == 0) {
                            rainPosX = x + xa;
                            rainPosY = y + 0.1f - Tile::tiles[t]->getShapeY0();
                            rainPosZ = z + za;
                        }
                        mc->particleEngine->add(std::make_shared<WaterDropParticle>(
                            level, x + xa, y + 0.1f - Tile::tiles[t]->getShapeY0(), z + za));
                    }
                }
            }
        }

        if (rainPosSamples > 0 && random->nextInt(3) < g_rainSoundTime++) {
            g_rainSoundTime = 0;
            if (rainPosY > player->y + 1 &&
                level->getTopRainBlock(Mth::floor(player->x), Mth::floor(player->z)) > Mth::floor(player->y)) {
                level->playLocalSound(rainPosX, rainPosY, rainPosZ, eSoundType_AMBIENT_WEATHER_RAIN, 0.1f, 0.5f, false);
            } else {
                level->playLocalSound(rainPosX, rainPosY, rainPosZ, eSoundType_AMBIENT_WEATHER_RAIN, 0.2f, 1.0f, false);
            }
        }
    } while (false);

    // the end of GameRenderer::tick
    g_tick.store(_tick + 1, std::memory_order_relaxed);
}

void mcle_weather_render(const std::shared_ptr<LivingEntity> &player, Level *level, float a) {
    static Random s_random;
    Random *random = &s_random;
    Minecraft *mc = Minecraft::GetInstance();
    int _tick = g_tick.load(std::memory_order_relaxed);

    float rainLevel = level->getRainLevel(a);
    if (rainLevel <= 0) return;

    if (g_rainXa == nullptr) {
        g_rainXa = new float[32 * 32];
        g_rainZa = new float[32 * 32];

        for (int z = 0; z < 32; z++) {
            for (int x = 0; x < 32; x++) {
                float xa = x - 16;
                float za = z - 16;
                float d = Mth::sqrt(xa * xa + za * za);
                g_rainXa[z << 5 | x] = -za / d;
                g_rainZa[z << 5 | x] = xa / d;
            }
        }
    }

    int x0 = Mth::floor(player->x);
    int y0 = Mth::floor(player->y);
    int z0 = Mth::floor(player->z);

    Tesselator *t = Tesselator::getInstance();
    glDisable(GL_CULL_FACE);
    glNormal3f(0, 1, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glAlphaFunc(GL_GREATER, 0.01f);

    mc->textures->bindTexture(&snowLocation());

    double xo = player->xOld + (player->x - player->xOld) * a;
    double yo = player->yOld + (player->y - player->yOld) * a;
    double zo = player->zOld + (player->z - player->zOld) * a;

    int yMin = Mth::floor(yo);

    int r = 5;
    switch (mc->levelRenderer->activePlayers()) {
    case 1:
    default:
        r = 9;
        break;
    case 2:
        r = 7;
        break;
    case 3:
        r = 5;
        break;
    case 4:
        r = 5;
        break;
    }

    int mode = -1;
    float time = _tick + a;

    glColor4f(1, 1, 1, 1);

    for (int x = x0 - r; x <= x0 + r; x++)
        for (int z = z0 - r; z <= z0 + r; z++) {
            int rainSlot = (z - z0 + 16) * 32 + (x - x0 + 16);
            float xa = g_rainXa[rainSlot] * 0.5f;
            float za = g_rainZa[rainSlot] * 0.5f;

            Biome *b = level->getBiome(x, z);
            if (!b->hasRain() && !b->hasSnow()) continue;

            int floor = level->getTopRainBlock(x, z);

            int yy0 = y0 - r;
            int yy1 = y0 + r;

            if (yy0 < floor) yy0 = floor;
            if (yy1 < floor) yy1 = floor;
            float s = 1;

            int yl = floor;
            if (yl < yMin) yl = yMin;

            if (yy0 != yy1) {
                random->setSeed((x * x * 3121 + x * 45238971) ^ (z * z * 418711 + z * 13761));

                float temp = b->getTemperature();
                if (level->getBiomeSource()->scaleTemp(temp, floor) >= 0.15f) {
                    if (mode != 0) {
                        if (mode >= 0) t->end();
                        mode = 0;
                        mc->textures->bindTexture(&rainLocation());
                        t->begin();
                    }

                    float ra = (((_tick + x * x * 3121 + x * 45238971 + z * z * 418711 + z * 13761) & 31) + a) / 32.0f *
                               (3 + random->nextFloat());

                    double xd = (x + 0.5f) - player->x;
                    double zd = (z + 0.5f) - player->z;
                    float dd = (float)Mth::sqrt(xd * xd + zd * zd) / r;

                    float br = 1;
                    t->offset(-xo * 1, -yo * 1, -zo * 1);
                    t->tex2(level->getLightColor(x, yl, z, 0));
                    t->color(br, br, br, ((1 - dd * dd) * 0.5f + 0.5f) * rainLevel);
                    t->vertexUV(x - xa + 0.5, yy0, z - za + 0.5, 0 * s, yy0 * s / 4.0f + ra * s);
                    t->vertexUV(x + xa + 0.5, yy0, z + za + 0.5, 1 * s, yy0 * s / 4.0f + ra * s);
                    // 4J - added to soften the top visible edge of the rain
                    t->color(br, br, br, 0.0f);
                    t->vertexUV(x + xa + 0.5, yy1, z + za + 0.5, 1 * s, yy1 * s / 4.0f + ra * s);
                    t->vertexUV(x - xa + 0.5, yy1, z - za + 0.5, 0 * s, yy1 * s / 4.0f + ra * s);
                    t->offset(0, 0, 0);
                    t->end();
                } else {
                    if (mode != 1) {
                        if (mode >= 0) t->end();
                        mode = 1;
                        mc->textures->bindTexture(&snowLocation());
                        t->begin();
                    }
                    float ra = (((_tick) & 511) + a) / 512.0f;
                    float uo = random->nextFloat() + time * 0.01f * static_cast<float>(random->nextGaussian());
                    float vo = random->nextFloat() + time * static_cast<float>(random->nextGaussian()) * 0.001f;
                    double xd = (x + 0.5f) - player->x;
                    double zd = (z + 0.5f) - player->z;
                    float dd = static_cast<float>(sqrt(xd * xd + zd * zd)) / r;
                    float br = 1;
                    t->offset(-xo * 1, -yo * 1, -zo * 1);
                    t->tex2((level->getLightColor(x, yl, z, 0) * 3 + 0xf000f0) / 4);
                    t->color(br, br, br, ((1 - dd * dd) * 0.3f + 0.5f) * rainLevel);
                    t->vertexUV(x - xa + 0.5, yy0, z - za + 0.5, 0 * s + uo, yy0 * s / 4.0f + ra * s + vo);
                    t->vertexUV(x + xa + 0.5, yy0, z + za + 0.5, 1 * s + uo, yy0 * s / 4.0f + ra * s + vo);
                    t->vertexUV(x + xa + 0.5, yy1, z + za + 0.5, 1 * s + uo, yy1 * s / 4.0f + ra * s + vo);
                    t->vertexUV(x - xa + 0.5, yy1, z - za + 0.5, 0 * s + uo, yy1 * s / 4.0f + ra * s + vo);
                    t->offset(0, 0, 0);
                }
            }
        }

    if (mode >= 0) t->end();
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glAlphaFunc(GL_GREATER, 0.1f);
}
