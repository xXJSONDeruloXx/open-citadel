#pragma once

#include <cstdint>

namespace open_citadel::audio {

int load_sound(const char *name);
int play_sound(int sound_id, bool loop);
void stop_sound(int stream_id);
void unload_sound(int sound_id);
void set_volume(int id, float volume);
void play_song(int descriptor, int64_t offset, int64_t length,
               const char *name);
void stop_song();
void update_song(float value);
void shutdown();

} // namespace open_citadel::audio
