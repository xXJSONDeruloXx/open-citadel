#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace open_citadel::audio {

struct OpenSlBufferQueue;
using OpenSlBufferCompletion = void (*)(void *context);

int load_sound(const char *name);
int play_sound(int sound_id, bool loop);
void stop_sound(int stream_id);
void unload_sound(int sound_id);
void set_volume(int id, float volume);
void play_song(int descriptor, int64_t offset, int64_t length,
               const char *name);
void stop_song();
void update_song(float value);
std::shared_ptr<OpenSlBufferQueue> create_open_sl_buffer_queue(
    uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample,
    size_t max_buffers, OpenSlBufferCompletion callback, void *context,
    std::shared_ptr<void> callback_lifetime = {});
bool enqueue_open_sl_buffer(const std::shared_ptr<OpenSlBufferQueue> &queue,
                            const void *buffer, size_t size);
void clear_open_sl_buffer_queue(
    const std::shared_ptr<OpenSlBufferQueue> &queue);
void set_open_sl_buffer_queue_playing(
    const std::shared_ptr<OpenSlBufferQueue> &queue, bool playing);
void set_open_sl_buffer_queue_gain(
    const std::shared_ptr<OpenSlBufferQueue> &queue, float gain);
void set_open_sl_buffer_queue_pan(
    const std::shared_ptr<OpenSlBufferQueue> &queue, int permille);
uint32_t get_open_sl_buffer_queue_count(
    const std::shared_ptr<OpenSlBufferQueue> &queue);
uint32_t get_open_sl_buffer_queue_index(
    const std::shared_ptr<OpenSlBufferQueue> &queue);
uint64_t get_open_sl_buffer_queue_position_ms(
    const std::shared_ptr<OpenSlBufferQueue> &queue);
void destroy_open_sl_buffer_queue(
    std::shared_ptr<OpenSlBufferQueue> *queue);
void shutdown();

} // namespace open_citadel::audio
