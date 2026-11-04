#pragma once

#include <memory>
#include <string>

class ServerPlayer;
class Level;

// Upstream SoundEngine, see MCLESound.cpp. documentsPath holds
// Windows64Media/Sound and music/.
void mcle_sound_init(const char *documentsPath);
// Hear the level's sounds from this player
void mcle_sound_listen(const std::shared_ptr<ServerPlayer> &player, Level *level);
// Minecraft's per frame soundEngine->tick plus the main loop's playMusicTick.
// Volumes are 0..1 like Options::set(MUSIC / SOUND).
void mcle_sound_tick(const std::shared_ptr<ServerPlayer> &player, float a, float musicVolume, float sfxVolume);
