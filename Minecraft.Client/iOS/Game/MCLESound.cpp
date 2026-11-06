// Upstream's miniaudio SoundEngine (Common/Audio/SoundEngine.cpp, the
// Windows64 one, patched by scripts/patch-soundengine-ios.py) built for
// iOS. Every other file sees the SoundEngine stub in iOS_stdafx.h, whose
// methods call the mcle_sound_* functions here. The real class is renamed
// in this file so the two don't clash.

#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <strings.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "MCLESound.h"

// Windows CRT calls SoundEngine.cpp makes
#undef sprintf_s
template <size_t N>
static int sprintf_s(char (&buf)[N], const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, N, fmt, ap);
    va_end(ap);
    return r;
}
static int sprintf_s(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}
static int strcpy_s(char *dst, size_t size, const char *src) {
    snprintf(dst, size, "%s", src);
    return 0;
}
static int fopen_s(FILE **f, const char *name, const char *mode) {
    *f = fopen(name, mode);
    return *f ? 0 : errno;
}
static int _stricmp(const char *a, const char *b) { return strcasecmp(a, b); }

// lce_filesystem's FileExists
static bool FileExists(const char *path) {
    DWORD attribs = GetFileAttributesA(path);
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
}

// The stub set this so the real header body is skipped everywhere else.
// The sound paths are private statics, so the class is opened up for this
// file only (its two includes go in first, unaffected).
#undef _SOUNDENGINE_H_DEFINED
#define SoundEngine MCLEUpstreamSoundEngine
#include "../../../upstream/Minecraft.World/SoundTypes.h"
#include "../../../upstream/Minecraft.Client/Common/Audio/miniaudio.h"
#include "../../../upstream/Minecraft.Client/Common/Audio/Consoles_SoundEngine.h"
#define private public
#include "../../../upstream/Minecraft.Client/Common/Audio/SoundEngine.h"
#undef private
#include "../../../upstream/Minecraft.Client/Common/Audio/SoundEngine.cpp"
#undef SoundEngine

#include "../../../upstream/Minecraft.World/LevelListener.h"
#include "../../../upstream/Minecraft.World/JavaMath.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"

namespace {

MCLEUpstreamSoundEngine *g_engine = nullptr;
// Sounds come from the sim thread, settings from the main thread
std::recursive_mutex g_mutex;
// The engine is made with the first world here, after a texture pack set
// its music. Upstream's is made at launch and has it set by then.
bool g_haveStreamingSounds = false;
int g_streamingSounds[7];
std::shared_ptr<ServerPlayer> g_listenerPlayer;
class ClientSoundListener;
// The listeners added to the current world's levels, dropped on exit
std::vector<std::pair<Level *, ClientSoundListener *>> g_listeners;

// There's no client level, so instead of ServerLevelListener's packets
// reaching ClientConnection::handleSoundEvent, the level's sounds come
// straight here. Sounds that skip their source player were already played
// by that player's own client, which here is this one, so they play too.
class ClientSoundListener : public LevelListener {
public:
    void tileChanged(int, int, int) override {}
    void tileLightChanged(int, int, int) override {}
    void setTilesDirty(int, int, int, int, int, int, Level *) override {}
    void allChanged() override {}
    // ClientConnection::handleSoundEvent
    void playSound(int iSound, double x, double y, double z, float volume, float pitch, float) override {
        mcle_level_play_local_sound(x, y, z, iSound, volume, pitch, false, 16.0f);
    }
    void playSoundExceptPlayer(shared_ptr<Player>, int iSound, double x, double y, double z, float volume,
                               float pitch, float) override {
        mcle_level_play_local_sound(x, y, z, iSound, volume, pitch, false, 16.0f);
    }
    void addParticle(ePARTICLE_TYPE, double, double, double, double, double, double) override {}
    void entityAdded(shared_ptr<Entity>) override {}
    void entityRemoved(shared_ptr<Entity>) override {}
    void playerRemoved(shared_ptr<Entity>) override {}
    void skyColorChanged() override {}
    void playStreamingMusic(const wstring &, int, int, int) override {}
    void globalLevelEvent(int, int, int, int, int) override {}
    void levelEvent(shared_ptr<Player>, int, int, int, int, int) override {}
    void destroyTileProgress(int, int, int, int, int) override {}
};

} // namespace

// MultiPlayerLevel::playLocalSound
void mcle_level_play_local_sound(double x, double y, double z, int iSound, float volume, float pitch,
                                 bool distanceDelay, float fClipSoundDist) {
    if (volume > 1) fClipSoundDist *= volume;

    float minDistSq = FLT_MAX;
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (g_listenerPlayer) minDistSq = static_cast<float>(g_listenerPlayer->distanceToSqr(x, y, z));
    }

    if (minDistSq < fClipSoundDist * fClipSoundDist) {
        if (distanceDelay && minDistSq > 10 * 10) {
            // exhaggerate sound speed effect by making speed of sound ~=
            // 40 m/s instead of 300 m/s
            double delayInSeconds = sqrt(minDistSq) / 40.0;
            mcle_sound_schedule(iSound, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), volume,
                                pitch, static_cast<int>(Math::round(delayInSeconds * SharedConstants::TICKS_PER_SECOND)));
        } else {
            mcle_sound_play(iSound, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), volume, pitch);
        }
    }
}

void mcle_sound_init(const char *documentsPath) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_engine) return;
    snprintf(MCLEUpstreamSoundEngine::m_szSoundPath, sizeof(MCLEUpstreamSoundEngine::m_szSoundPath),
             "%s/Windows64Media/Sound/", documentsPath);
    snprintf(MCLEUpstreamSoundEngine::m_szMusicPath, sizeof(MCLEUpstreamSoundEngine::m_szMusicPath),
             "%s/music/", documentsPath);
    g_engine = new MCLEUpstreamSoundEngine();
    g_engine->init(nullptr);
    if (g_haveStreamingSounds) {
        const int *s = g_streamingSounds;
        g_engine->SetStreamingSounds(s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
        // The texture pack reload's playStreaming, which came before there
        // was an engine to pick the pack's music
        g_engine->playStreaming(L"", 0, 0, 0, 1, 1);
    }
}

// SoundEngine::SetStreamingSounds, kept for the engine if it isn't made yet
void mcle_sound_set_streaming_sounds(int iOverworldMin, int iOverWorldMax, int iNetherMin, int iNetherMax,
                                     int iEndMin, int iEndMax, int iCD1) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    const int s[7] = {iOverworldMin, iOverWorldMax, iNetherMin, iNetherMax, iEndMin, iEndMax, iCD1};
    memcpy(g_streamingSounds, s, sizeof(s));
    g_haveStreamingSounds = true;
    if (g_engine) g_engine->SetStreamingSounds(iOverworldMin, iOverWorldMax, iNetherMin, iNetherMax, iEndMin, iEndMax, iCD1);
}

void mcle_sound_set_default_streaming_sounds() {
    mcle_sound_set_streaming_sounds(eStream_Overworld_Calm1, eStream_Overworld_piano3, eStream_Nether1, eStream_Nether4,
                                    eStream_end_dragon, eStream_end_end, eStream_CD_1);
}

void mcle_sound_listen(const std::shared_ptr<ServerPlayer> &player, Level *level) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_listenerPlayer = player;
    if (level) {
        ClientSoundListener *listener = new ClientSoundListener();
        level->addListener(listener);
        g_listeners.emplace_back(level, listener);
    }
}

void mcle_sound_unlisten() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    for (auto &entry : g_listeners) {
        entry.first->removeListener(entry.second);
        delete entry.second;
    }
    g_listeners.clear();
    g_listenerPlayer.reset();
}

// The in-game music stream, as the Stop state of playMusicUpdate leaves it.
// The menu music is a separate player on iOS.
void mcle_sound_stop_music() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_engine) return;
    // A stream still opening finishes on its own thread first
    for (int i = 0; i < 200 && g_engine->m_openStreamThread && g_engine->m_openStreamThread->isRunning(); ++i) {
        usleep(10 * 1000);
    }
    if (g_engine->m_openStreamThread && !g_engine->m_openStreamThread->isRunning()) {
        delete g_engine->m_openStreamThread;
        g_engine->m_openStreamThread = nullptr;
    }
    if (g_engine->m_musicStreamActive) {
        ma_sound_stop(&g_engine->m_musicStream);
        ma_sound_uninit(&g_engine->m_musicStream);
        g_engine->m_musicStreamActive = false;
    }
    g_engine->SetIsPlayingStreamingCDMusic(false);
    g_engine->SetIsPlayingStreamingGameMusic(false);
    g_engine->m_StreamState = eMusicStreamState_Idle;
}

void mcle_sound_tick(const std::shared_ptr<ServerPlayer> &player, float a, float musicVolume, float sfxVolume) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_engine) return;
    // ActionGameSettings -> Options::set(MUSIC / SOUND)
    g_engine->updateMusicVolume(musicVolume);
    g_engine->updateSoundEffectVolume(sfxVolume);
    // Minecraft passes localplayers cast to shared_ptr<Mob>*, the same here
    shared_ptr<Mob> players[MAX_LOCAL_PLAYERS];
    if (player) players[0] = shared_ptr<Mob>(player, reinterpret_cast<Mob *>(player.get()));
    g_engine->tick(players, a);
    g_engine->playMusicTick();
}

void mcle_sound_play(int iSound, float x, float y, float z, float volume, float pitch) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_engine) g_engine->play(iSound, x, y, z, volume, pitch);
}

void mcle_sound_schedule(int iSound, float x, float y, float z, float volume, float pitch, int delayTicks) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_engine) g_engine->schedule(iSound, x, y, z, volume, pitch, delayTicks);
}

void mcle_sound_play_ui(int iSound, float volume, float pitch) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_engine) g_engine->playUI(iSound, volume, pitch);
}

void mcle_sound_play_streaming(const std::wstring &name, float x, float y, float z, float volume, float pitch,
                               bool bMusicDelay) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_engine) g_engine->playStreaming(name, x, y, z, volume, pitch, bMusicDelay);
}

bool mcle_sound_is_playing_game_music() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    return g_engine ? g_engine->GetIsPlayingStreamingGameMusic() : false;
}

void mcle_sound_update_music_volume(float v) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_engine) g_engine->updateMusicVolume(v);
}

void mcle_sound_update_sfx_volume(float v) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_engine) g_engine->updateSoundEffectVolume(v);
}
