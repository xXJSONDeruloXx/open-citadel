#include "citadel_audio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <climits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <mpg123.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fix_path.h"
#include "trace.h"

namespace open_citadel::audio {

struct OpenSlBufferQueue {
    struct Buffer {
        std::vector<int16_t> pcm;
        size_t position_frames = 0;
    };

    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint32_t bits_per_sample = 0;
    int output_rate = 0;
    size_t max_buffers = 0;
    std::deque<Buffer> buffers;
    bool playing = false;
    float gain = 1.0f;
    int pan_permille = 0;
    uint64_t played_frames = 0;
    uint32_t buffer_index = 0;
    OpenSlBufferCompletion callback = nullptr;
    void *callback_context = nullptr;
    std::shared_ptr<void> callback_lifetime;
    std::atomic<bool> destroying{false};
    std::atomic<size_t> callbacks_in_flight{0};
    std::mutex callback_mutex;
    std::condition_variable callback_finished;
};

namespace {

namespace fs = std::filesystem;

constexpr size_t kMaxAudioFileBytes = 64u * 1024u * 1024u;
constexpr size_t kSoundCount = 64;
constexpr size_t kVoiceCount = 32;
constexpr int kVoiceIdBase = 1000;
constexpr int kOutputRate = 44100;
constexpr size_t kOpenSlQueueCount = 32;
constexpr size_t kMaxOpenSlBuffersPerQueue = 32;
constexpr size_t kMaxOpenSlCompletionJobs =
    kOpenSlQueueCount * kMaxOpenSlBuffersPerQueue;
constexpr size_t kMaxOpenSlBufferBytes = 16u * 1024u * 1024u;

struct SoundSample {
    bool loaded = false;
    float volume = 1.0f;
    std::string name;
    std::shared_ptr<std::vector<int16_t>> pcm;
};

struct Voice {
    bool playing = false;
    bool loop = false;
    float volume = 1.0f;
    size_t position = 0;
    int sample_id = 0;
    std::shared_ptr<std::vector<int16_t>> pcm;
};

std::mutex g_init_mutex;
SDL_AudioDeviceID g_audio_device = 0;
SDL_AudioSpec g_audio_spec{};
SDL_mutex *g_audio_mutex = nullptr;
bool g_initialized = false;
bool g_mpg123_initialized = false;
std::array<SoundSample, kSoundCount> g_sounds;
std::array<Voice, kVoiceCount> g_voices;
Voice g_song;
std::array<std::shared_ptr<OpenSlBufferQueue>, kOpenSlQueueCount>
    g_open_sl_queues;
std::mutex g_open_sl_api_mutex;
std::atomic<bool> g_audio_shutting_down{false};
std::atomic<unsigned int> g_trace_events{0};

void trace_audio(const char *operation, int id, const char *name = nullptr)
{
    if (g_trace_events.fetch_add(1, std::memory_order_relaxed) >= 64)
        return;
    trace("OpenCitadel audio %s id=%d%s%s%s", operation, id,
          name ? " name='" : "", name ? name : "", name ? "'" : "");
}

float clamp_volume(float volume)
{
    if (!std::isfinite(volume))
        return 0.0f;
    return std::clamp(volume, 0.0f, 1.0f);
}

int16_t clamp_sample(int value)
{
    if (value > 32767)
        return 32767;
    if (value < -32768)
        return -32768;
    return static_cast<int16_t>(value);
}

void mix_voice(Voice &voice, int16_t *output, size_t frames)
{
    if (!voice.playing || !voice.pcm || voice.pcm->size() < 2)
        return;

    const std::vector<int16_t> &pcm = *voice.pcm;
    for (size_t frame = 0; frame < frames; ++frame) {
        if (voice.position + 1 >= pcm.size()) {
            if (voice.loop) {
                voice.position = 0;
            } else {
                voice.playing = false;
                break;
            }
        }

        const size_t output_index = frame * 2;
        const int left = static_cast<int>(output[output_index]) +
                         static_cast<int>(pcm[voice.position] * voice.volume);
        const int right = static_cast<int>(output[output_index + 1]) +
                          static_cast<int>(pcm[voice.position + 1] * voice.volume);
        output[output_index] = clamp_sample(left);
        output[output_index + 1] = clamp_sample(right);
        voice.position += 2;
    }
}

struct OpenSlCompletionJob {
    std::shared_ptr<OpenSlBufferQueue> queue;
    std::shared_ptr<void> callback_lifetime;
    OpenSlBufferCompletion callback = nullptr;
    void *context = nullptr;
};

thread_local bool g_inside_open_sl_completion = false;

void mix_open_sl_queue(
    const std::shared_ptr<OpenSlBufferQueue> &queue, int16_t *output,
    size_t frames,
    std::array<OpenSlCompletionJob, kMaxOpenSlCompletionJobs> *jobs,
    size_t *job_count)
{
    if (!queue || !queue->playing || queue->destroying.load(
            std::memory_order_relaxed))
        return;

    const float pan = static_cast<float>(queue->pan_permille) / 1000.0f;
    const float left_gain = queue->gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
    const float right_gain = queue->gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    size_t output_frame = 0;
    while (output_frame < frames && !queue->buffers.empty()) {
        OpenSlBufferQueue::Buffer &buffer = queue->buffers.front();
        const size_t buffer_frames = buffer.pcm.size() / 2;
        if (buffer.position_frames >= buffer_frames) {
            queue->buffers.pop_front();
            ++queue->buffer_index;
            if (queue->callback && *job_count < jobs->size()) {
                queue->callbacks_in_flight.fetch_add(
                    1, std::memory_order_relaxed);
                (*jobs)[(*job_count)++] = {
                    queue, queue->callback_lifetime, queue->callback,
                    queue->callback_context};
            }
            continue;
        }

        const size_t frame_count = std::min(
            frames - output_frame, buffer_frames - buffer.position_frames);
        for (size_t frame = 0; frame < frame_count; ++frame) {
            const size_t source_index = (buffer.position_frames + frame) * 2;
            const size_t output_index = (output_frame + frame) * 2;
            const int left = static_cast<int>(output[output_index]) +
                static_cast<int>(buffer.pcm[source_index] * left_gain);
            const int right = static_cast<int>(output[output_index + 1]) +
                static_cast<int>(buffer.pcm[source_index + 1] * right_gain);
            output[output_index] = clamp_sample(left);
            output[output_index + 1] = clamp_sample(right);
        }
        buffer.position_frames += frame_count;
        queue->played_frames += frame_count;
        output_frame += frame_count;
        if (buffer.position_frames == buffer_frames) {
            queue->buffers.pop_front();
            ++queue->buffer_index;
            if (queue->callback && *job_count < jobs->size()) {
                queue->callbacks_in_flight.fetch_add(
                    1, std::memory_order_relaxed);
                (*jobs)[(*job_count)++] = {
                    queue, queue->callback_lifetime, queue->callback,
                    queue->callback_context};
            }
        }
    }
}

void audio_callback(void *, Uint8 *stream, int length)
{
    if (!stream || length <= 0)
        return;
    std::memset(stream, 0, static_cast<size_t>(length));
    if (!g_audio_mutex || g_audio_spec.format != AUDIO_S16SYS ||
        g_audio_spec.channels != 2)
        return;

    int16_t *output = reinterpret_cast<int16_t *>(stream);
    const size_t frames = static_cast<size_t>(length) / (sizeof(int16_t) * 2);
    std::array<OpenSlCompletionJob, kMaxOpenSlCompletionJobs> completion_jobs;
    size_t completion_count = 0;
    SDL_LockMutex(g_audio_mutex);
    mix_voice(g_song, output, frames);
    for (Voice &voice : g_voices)
        mix_voice(voice, output, frames);
    for (const auto &queue : g_open_sl_queues)
        mix_open_sl_queue(queue, output, frames, &completion_jobs,
                          &completion_count);
    SDL_UnlockMutex(g_audio_mutex);

    for (size_t i = 0; i < completion_count; ++i) {
        OpenSlCompletionJob &job = completion_jobs[i];
        g_inside_open_sl_completion = true;
        try {
            job.callback(job.context);
        } catch (...) {
            trace("OpenCitadel OpenSL buffer callback threw an exception");
        }
        g_inside_open_sl_completion = false;
        if (job.queue->callbacks_in_flight.fetch_sub(
                1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lock(job.queue->callback_mutex);
            job.queue->callback_finished.notify_all();
        }
    }
}

void ensure_initialized()
{
    std::lock_guard<std::mutex> guard(g_init_mutex);
    if (g_initialized)
        return;
    g_initialized = true;
    g_audio_shutting_down.store(false, std::memory_order_release);

    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) &&
        SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        trace("OpenCitadel audio SDL init failed: %s", SDL_GetError());
    }

    g_audio_mutex = SDL_CreateMutex();
    if (!g_audio_mutex) {
        trace("OpenCitadel audio mutex failed: %s", SDL_GetError());
    } else {
        SDL_AudioSpec desired{};
        desired.freq = kOutputRate;
        desired.format = AUDIO_S16SYS;
        desired.channels = 2;
        desired.samples = 1024;
        desired.callback = audio_callback;
        g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired,
                                             &g_audio_spec,
                                             SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (!g_audio_device) {
            trace("OpenCitadel audio output unavailable: %s", SDL_GetError());
        } else if (g_audio_spec.format != AUDIO_S16SYS ||
                   g_audio_spec.channels != 2) {
            trace("OpenCitadel audio output format unsupported: %d Hz/0x%x/%d",
                  g_audio_spec.freq, g_audio_spec.format,
                  g_audio_spec.channels);
            SDL_CloseAudioDevice(g_audio_device);
            g_audio_device = 0;
        } else {
            trace("OpenCitadel audio output: %d Hz, stereo",
                  g_audio_spec.freq);
            SDL_PauseAudioDevice(g_audio_device, 0);
        }
    }

    const int mpg_error = mpg123_init();
    if (mpg_error == MPG123_OK) {
        g_mpg123_initialized = true;
    } else {
        trace("OpenCitadel audio mpg123 init failed: %s",
              mpg123_plain_strerror(mpg_error));
    }
}

bool convert_audio(const SDL_AudioSpec &source, const Uint8 *data,
                   Uint32 length, std::vector<int16_t> *output)
{
    if (!data || !length || length > static_cast<Uint32>(INT_MAX) ||
        !source.freq || !source.channels || !output)
        return false;

    const int target_rate = g_audio_spec.freq > 0 ? g_audio_spec.freq : kOutputRate;
    SDL_AudioStream *stream = SDL_NewAudioStream(
        source.format, source.channels, source.freq,
        AUDIO_S16SYS, 2, target_rate);
    if (!stream)
        return false;

    bool okay = SDL_AudioStreamPut(stream, data, static_cast<int>(length)) == 0 &&
                SDL_AudioStreamFlush(stream) == 0;
    if (okay) {
        const int available = SDL_AudioStreamAvailable(stream);
        if (available <= 0 || (available & (static_cast<int>(sizeof(int16_t)) - 1))) {
            okay = false;
        } else {
            output->resize(static_cast<size_t>(available) / sizeof(int16_t));
            if (SDL_AudioStreamGet(stream, output->data(), available) != available)
                okay = false;
        }
    }
    SDL_FreeAudioStream(stream);
    if (!okay)
        output->clear();
    return okay;
}

bool convert_open_sl_pcm(uint32_t sample_rate, uint32_t channels,
                         uint32_t bits_per_sample, int output_rate,
                         const void *buffer, size_t size,
                         std::vector<int16_t> *output)
{
    if (!buffer || !output || !sample_rate || !output_rate ||
        (channels != 1 && channels != 2) || bits_per_sample != 16 ||
        !size || size > kMaxOpenSlBufferBytes ||
        size % (channels * sizeof(int16_t)) != 0 ||
        size > static_cast<size_t>(INT_MAX))
        return false;

    SDL_AudioStream *stream = SDL_NewAudioStream(
        AUDIO_S16LSB, static_cast<Uint8>(channels),
        static_cast<int>(sample_rate), AUDIO_S16SYS, 2, output_rate);
    if (!stream)
        return false;

    bool okay = SDL_AudioStreamPut(stream, static_cast<const Uint8 *>(buffer),
                                   static_cast<int>(size)) == 0 &&
                SDL_AudioStreamFlush(stream) == 0;
    if (okay) {
        const int available = SDL_AudioStreamAvailable(stream);
        constexpr int output_frame_bytes = sizeof(int16_t) * 2;
        if (available <= 0 ||
            static_cast<size_t>(available) > kMaxOpenSlBufferBytes ||
            available % output_frame_bytes != 0) {
            okay = false;
        } else {
            try {
                output->resize(static_cast<size_t>(available) /
                               sizeof(int16_t));
            } catch (...) {
                okay = false;
            }
            if (okay && SDL_AudioStreamGet(stream, output->data(), available) !=
                            available)
                okay = false;
        }
    }
    SDL_FreeAudioStream(stream);
    if (!okay)
        output->clear();
    return okay;
}

bool open_sl_queue_is_registered(
    const std::shared_ptr<OpenSlBufferQueue> &queue)
{
    return std::any_of(g_open_sl_queues.begin(), g_open_sl_queues.end(),
                       [&queue](const auto &entry) {
                           return entry.get() == queue.get();
                       });
}

bool decode_wav(const Uint8 *data, size_t length,
                std::vector<int16_t> *output)
{
    if (!data || !length || length > static_cast<size_t>(INT_MAX))
        return false;

    SDL_RWops *source_stream = SDL_RWFromConstMem(data, static_cast<int>(length));
    if (!source_stream)
        return false;

    SDL_AudioSpec source{};
    Uint8 *decoded = nullptr;
    Uint32 decoded_length = 0;
    if (!SDL_LoadWAV_RW(source_stream, 1, &source, &decoded, &decoded_length))
        return false;
    const bool okay = convert_audio(source, decoded, decoded_length, output);
    SDL_FreeWAV(decoded);
    return okay;
}

bool decode_mp3(const Uint8 *data, size_t length,
                std::vector<int16_t> *output)
{
    if (!g_mpg123_initialized || !data || !length)
        return false;

    int error = MPG123_OK;
    mpg123_handle *decoder = mpg123_new(nullptr, &error);
    if (!decoder)
        return false;

    bool okay = false;
    std::vector<Uint8> decoded;
    long source_rate = 0;
    int source_channels = 0;
    int source_encoding = 0;
    const long *rates = nullptr;
    size_t rate_count = 0;

    if (mpg123_format_none(decoder) == MPG123_OK) {
        mpg123_rates(&rates, &rate_count);
        bool formats_ready = rate_count > 0;
        for (size_t i = 0; i < rate_count; ++i) {
            if (mpg123_format(decoder, rates[i], MPG123_MONO | MPG123_STEREO,
                              MPG123_ENC_SIGNED_16) != MPG123_OK) {
                formats_ready = false;
                break;
            }
        }
        if (formats_ready && mpg123_open_feed(decoder) == MPG123_OK &&
            mpg123_feed(decoder, data, length) == MPG123_OK) {
            Uint8 block[16384];
            bool have_format = false;
            bool decode_failed = false;
            for (size_t iteration = 0; iteration < 100000; ++iteration) {
                size_t received = 0;
                const int result = mpg123_read(decoder, block, sizeof(block),
                                               &received);
                if (result == MPG123_NEW_FORMAT || !have_format) {
                    if (mpg123_getformat(decoder, &source_rate,
                                         &source_channels,
                                         &source_encoding) != MPG123_OK ||
                        source_rate <= 0 || source_channels < 1 ||
                        source_channels > 2 ||
                        source_encoding != MPG123_ENC_SIGNED_16) {
                        decode_failed = true;
                        break;
                    }
                    have_format = true;
                }
                if (received)
                    decoded.insert(decoded.end(), block, block + received);
                if (result == MPG123_DONE || result == MPG123_NEED_MORE)
                    break;
                if (result != MPG123_OK && result != MPG123_NEW_FORMAT) {
                    decode_failed = true;
                    break;
                }
            }

            if (!decode_failed && have_format && !decoded.empty() &&
                decoded.size() <= static_cast<size_t>(INT_MAX)) {
                SDL_AudioSpec source{};
                source.freq = static_cast<int>(source_rate);
                source.channels = static_cast<Uint8>(source_channels);
                source.format = AUDIO_S16LSB;
                okay = convert_audio(source, decoded.data(),
                                     static_cast<Uint32>(decoded.size()), output);
            }
        }
    }

    if (!okay)
        trace("OpenCitadel audio MP3 decode failed: %s",
              mpg123_strerror(decoder));
    mpg123_close(decoder);
    mpg123_delete(decoder);
    return okay;
}

std::string normalized_name(const char *input)
{
    std::string name = input ? input : "";
    std::replace(name.begin(), name.end(), '\\', '/');
    const char *prefixes[] = {"appbundle:/", "file:///", "file://", "assets/"};
    for (size_t pass = 0;
         pass < sizeof(prefixes) / sizeof(prefixes[0]); ++pass) {
        bool stripped = false;
        for (const char *prefix : prefixes) {
            if (name.rfind(prefix, 0) == 0) {
                name.erase(0, std::strlen(prefix));
                stripped = true;
                break;
            }
        }
        if (!stripped)
            break;
    }
    while (!name.empty() && name.front() == '/')
        name.erase(name.begin());
    while (name.rfind("./", 0) == 0)
        name.erase(0, 2);
    return name;
}

bool safe_relative_name(const std::string &name)
{
    if (name.empty())
        return false;
    const fs::path relative = fs::u8path(name);
    if (relative.is_absolute() || relative.has_root_name() ||
        relative.has_root_directory())
        return false;
    for (const fs::path &part : relative) {
        if (part.empty() || part == "." || part == "..")
            return false;
    }
    return true;
}

std::string extension_lower(const fs::path &path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension;
}

std::string path_utf8(const fs::path &path)
{
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char *>(encoded.data()),
                       encoded.size());
}

bool resolve_audio_path(const char *input, fs::path *resolved)
{
    if (!resolved)
        return false;
    const std::string name = normalized_name(input);
    if (!safe_relative_name(name))
        return false;

    const char *root_text = io_game_dir();
    if (!root_text || !*root_text)
        return false;
    const fs::path root = fs::u8path(root_text);
    const fs::path relative = fs::u8path(name);
    const std::array<fs::path, 4> bases = {
        root,
        root / "assets",
        root / "UDKGame" / "Audio",
        root / "UDKGame" / "Music",
    };
    const std::array<std::string, 3> extensions = {".wav", ".mp3", ".ogg"};
    const bool append_extension = relative.extension().empty();

    for (const fs::path &base : bases) {
        const fs::path candidate = base / relative;
        std::error_code error;
        if (fs::is_regular_file(candidate, error)) {
            *resolved = candidate;
            return true;
        }
        if (append_extension) {
            for (const std::string &extension : extensions) {
                fs::path with_extension = candidate;
                with_extension += extension;
                error.clear();
                if (fs::is_regular_file(with_extension, error)) {
                    *resolved = with_extension;
                    return true;
                }
            }
        }
    }
    return false;
}

bool read_file(const fs::path &path, std::vector<Uint8> *data)
{
    std::error_code error;
    const uintmax_t length = fs::file_size(path, error);
    if (error || length == 0 || length > kMaxAudioFileBytes)
        return false;

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    data->resize(static_cast<size_t>(length));
    file.read(reinterpret_cast<char *>(data->data()),
              static_cast<std::streamsize>(data->size()));
    return file.good() || (file.eof() &&
           static_cast<size_t>(file.gcount()) == data->size());
}

bool read_descriptor(int descriptor, int64_t offset, int64_t length,
                     std::vector<Uint8> *data)
{
    if (descriptor < 0 || offset < 0 || length <= 0 ||
        static_cast<uint64_t>(length) > kMaxAudioFileBytes)
        return false;

#if defined(_WIN32)
    const __int64 original = _telli64(descriptor);
    if (original < 0 || _lseeki64(descriptor, offset, SEEK_SET) < 0)
        return false;
#else
    const off_t original = lseek(descriptor, 0, SEEK_CUR);
    if (original < 0 || lseek(descriptor, static_cast<off_t>(offset), SEEK_SET) < 0)
        return false;
#endif

    data->resize(static_cast<size_t>(length));
    size_t done = 0;
    bool okay = true;
    while (done < data->size()) {
        const size_t remaining = data->size() - done;
        const unsigned int chunk = static_cast<unsigned int>(
            std::min<size_t>(remaining, 64u * 1024u));
#if defined(_WIN32)
        const int received = _read(descriptor, data->data() + done, chunk);
#else
        const ssize_t received = read(descriptor, data->data() + done, chunk);
#endif
        if (received <= 0) {
            okay = false;
            break;
        }
        done += static_cast<size_t>(received);
    }

#if defined(_WIN32)
    if (_lseeki64(descriptor, original, SEEK_SET) < 0)
#else
    if (lseek(descriptor, original, SEEK_SET) < 0)
#endif
        okay = false;
    return okay;
}

bool decode_audio(const Uint8 *data, size_t length, const char *name,
                  std::vector<int16_t> *pcm)
{
    if (!data || !length || !pcm)
        return false;

    const std::string extension = extension_lower(fs::u8path(name ? name : ""));
    const bool is_wav = extension == ".wav" ||
                        (length >= 4 && std::memcmp(data, "RIFF", 4) == 0);
    if (is_wav)
        return decode_wav(data, length, pcm);
    if (extension == ".mp3" ||
        (length >= 3 && std::memcmp(data, "ID3", 3) == 0) ||
        (length >= 2 && data[0] == 0xff && (data[1] & 0xe0) == 0xe0))
        return decode_mp3(data, length, pcm);
    return false;
}

int play_sample(int sound_id, bool loop)
{
    if (sound_id <= 0 || static_cast<size_t>(sound_id) > g_sounds.size())
        return 0;

    ensure_initialized();
    if (!g_audio_mutex || !g_audio_device)
        return 0;

    SDL_LockMutex(g_audio_mutex);
    SoundSample &sample = g_sounds[static_cast<size_t>(sound_id - 1)];
    if (!sample.loaded || !sample.pcm || sample.pcm->empty()) {
        SDL_UnlockMutex(g_audio_mutex);
        return 0;
    }

    for (size_t i = 0; i < g_voices.size(); ++i) {
        Voice &voice = g_voices[i];
        if (voice.playing)
            continue;
        voice = Voice{};
        voice.playing = true;
        voice.loop = loop;
        voice.volume = sample.volume;
        voice.sample_id = sound_id;
        voice.pcm = sample.pcm;
        const int stream_id = kVoiceIdBase + static_cast<int>(i) + 1;
        trace_audio("play sound", stream_id, sample.name.c_str());
        SDL_UnlockMutex(g_audio_mutex);
        return stream_id;
    }

    SDL_UnlockMutex(g_audio_mutex);
    trace_audio("voice limit reached", sound_id);
    return 0;
}

} // namespace

int load_sound(const char *name)
{
    ensure_initialized();
    fs::path path;
    if (!resolve_audio_path(name, &path)) {
        trace_audio("sound missing", -1, name ? name : "(null)");
        return -1;
    }

    const std::string resolved_name = path_utf8(path);
    if (g_audio_mutex) {
        SDL_LockMutex(g_audio_mutex);
        for (size_t i = 0; i < g_sounds.size(); ++i) {
            if (g_sounds[i].loaded && g_sounds[i].name == resolved_name) {
                SDL_UnlockMutex(g_audio_mutex);
                return static_cast<int>(i + 1);
            }
        }
        SDL_UnlockMutex(g_audio_mutex);
    }

    std::vector<Uint8> bytes;
    std::vector<int16_t> decoded;
    if (!read_file(path, &bytes) ||
        !decode_audio(bytes.data(), bytes.size(), resolved_name.c_str(), &decoded) ||
        decoded.empty()) {
        trace_audio("sound decode failed", -1, resolved_name.c_str());
        return -1;
    }

    auto pcm = std::make_shared<std::vector<int16_t>>(std::move(decoded));
    if (!g_audio_mutex)
        return -1;
    SDL_LockMutex(g_audio_mutex);
    for (size_t i = 0; i < g_sounds.size(); ++i) {
        SoundSample &sample = g_sounds[i];
        if (!sample.loaded) {
            sample.loaded = true;
            sample.volume = 1.0f;
            sample.name = resolved_name;
            sample.pcm = std::move(pcm);
            trace_audio("sound loaded", static_cast<int>(i + 1),
                        sample.name.c_str());
            SDL_UnlockMutex(g_audio_mutex);
            return static_cast<int>(i + 1);
        }
    }
    SDL_UnlockMutex(g_audio_mutex);
    trace_audio("sound pool full", -1, resolved_name.c_str());
    return -1;
}

int play_sound(int sound_id, bool loop)
{
    return play_sample(sound_id, loop);
}

void stop_sound(int stream_id)
{
    ensure_initialized();
    if (!g_audio_mutex)
        return;
    SDL_LockMutex(g_audio_mutex);
    if (stream_id > kVoiceIdBase &&
        stream_id <= kVoiceIdBase + static_cast<int>(g_voices.size())) {
        g_voices[static_cast<size_t>(stream_id - kVoiceIdBase - 1)].playing = false;
    } else if (stream_id > 0 &&
               static_cast<size_t>(stream_id) <= g_sounds.size()) {
        for (Voice &voice : g_voices) {
            if (voice.sample_id == stream_id)
                voice.playing = false;
        }
    }
    SDL_UnlockMutex(g_audio_mutex);
}

void unload_sound(int sound_id)
{
    ensure_initialized();
    if (sound_id <= 0 || static_cast<size_t>(sound_id) > g_sounds.size() ||
        !g_audio_mutex)
        return;
    SDL_LockMutex(g_audio_mutex);
    g_sounds[static_cast<size_t>(sound_id - 1)] = SoundSample{};
    SDL_UnlockMutex(g_audio_mutex);
}

void set_volume(int id, float volume)
{
    ensure_initialized();
    if (!g_audio_mutex)
        return;
    const float clamped = clamp_volume(volume);
    SDL_LockMutex(g_audio_mutex);
    if (id == 0) {
        g_song.volume = clamped;
    } else if (id > kVoiceIdBase &&
               id <= kVoiceIdBase + static_cast<int>(g_voices.size())) {
        g_voices[static_cast<size_t>(id - kVoiceIdBase - 1)].volume = clamped;
    } else if (id > 0 && static_cast<size_t>(id) <= g_sounds.size()) {
        SoundSample &sample = g_sounds[static_cast<size_t>(id - 1)];
        sample.volume = clamped;
        for (Voice &voice : g_voices) {
            if (voice.sample_id == id)
                voice.volume = clamped;
        }
    }
    SDL_UnlockMutex(g_audio_mutex);
}

void play_song(int descriptor, int64_t offset, int64_t length,
               const char *name)
{
    ensure_initialized();
    std::vector<Uint8> bytes;
    std::vector<int16_t> decoded;
    if (!g_audio_device || !read_descriptor(descriptor, offset, length, &bytes) ||
        !decode_audio(bytes.data(), bytes.size(), name, &decoded) ||
        decoded.empty()) {
        trace_audio("song decode failed", -1, name ? name : "(unnamed)");
        return;
    }

    auto pcm = std::make_shared<std::vector<int16_t>>(std::move(decoded));
    SDL_LockMutex(g_audio_mutex);
    g_song = Voice{};
    g_song.playing = true;
    g_song.loop = true;
    g_song.volume = 1.0f;
    g_song.pcm = std::move(pcm);
    trace_audio("song playing", 0, name ? name : "(unnamed)");
    SDL_UnlockMutex(g_audio_mutex);
}

void stop_song()
{
    ensure_initialized();
    if (!g_audio_mutex)
        return;
    SDL_LockMutex(g_audio_mutex);
    g_song.playing = false;
    g_song.pcm.reset();
    SDL_UnlockMutex(g_audio_mutex);
}

void update_song(float value)
{
    static std::atomic<bool> reported{false};
    if (!reported.exchange(true, std::memory_order_relaxed))
        trace("OpenCitadel audio song update callback received (%.3f)", value);
}

std::shared_ptr<OpenSlBufferQueue> create_open_sl_buffer_queue(
    uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample,
    size_t max_buffers, OpenSlBufferCompletion callback, void *context,
    std::shared_ptr<void> callback_lifetime)
{
    if (!sample_rate || (channels != 1 && channels != 2) ||
        bits_per_sample != 16 || !max_buffers)
        return {};

    ensure_initialized();
    std::shared_ptr<OpenSlBufferQueue> queue;
    try {
        queue = std::make_shared<OpenSlBufferQueue>();
    } catch (...) {
        return {};
    }
    queue->sample_rate = sample_rate;
    queue->channels = channels;
    queue->bits_per_sample = bits_per_sample;
    queue->max_buffers = std::min(max_buffers, kMaxOpenSlBuffersPerQueue);
    queue->callback = callback;
    queue->callback_context = context;
    queue->callback_lifetime = std::move(callback_lifetime);

    std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
    if (g_audio_shutting_down.load(std::memory_order_acquire) ||
        !g_audio_mutex)
        return {};
    queue->output_rate = g_audio_spec.freq > 0 ? g_audio_spec.freq : kOutputRate;
    SDL_LockMutex(g_audio_mutex);
    for (auto &slot : g_open_sl_queues) {
        if (slot)
            continue;
        slot = queue;
        SDL_UnlockMutex(g_audio_mutex);
        trace("OpenCitadel OpenSL audio queue created (%u Hz, %u ch, %zu max)",
              sample_rate, channels, queue->max_buffers);
        return queue;
    }
    SDL_UnlockMutex(g_audio_mutex);
    trace("OpenCitadel OpenSL audio queue limit reached");
    return {};
}

bool enqueue_open_sl_buffer(const std::shared_ptr<OpenSlBufferQueue> &queue,
                            const void *buffer, size_t size)
{
    if (!queue || queue->destroying.load(std::memory_order_acquire))
        return false;
    std::vector<int16_t> pcm;
    if (!convert_open_sl_pcm(queue->sample_rate, queue->channels,
                             queue->bits_per_sample, queue->output_rate,
                             buffer, size, &pcm) || pcm.empty())
        return false;

    std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
    if (g_audio_shutting_down.load(std::memory_order_acquire) ||
        !g_audio_mutex)
        return false;
    SDL_LockMutex(g_audio_mutex);
    if (queue->destroying.load(std::memory_order_relaxed) ||
        !open_sl_queue_is_registered(queue) ||
        queue->buffers.size() >= queue->max_buffers) {
        SDL_UnlockMutex(g_audio_mutex);
        return false;
    }
    try {
        queue->buffers.push_back({std::move(pcm), 0});
    } catch (...) {
        SDL_UnlockMutex(g_audio_mutex);
        return false;
    }
    SDL_UnlockMutex(g_audio_mutex);
    return true;
}

void clear_open_sl_buffer_queue(
    const std::shared_ptr<OpenSlBufferQueue> &queue)
{
    if (!queue)
        return;
    std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
    if (g_audio_shutting_down.load(std::memory_order_acquire) ||
        !g_audio_mutex)
        return;
    SDL_LockMutex(g_audio_mutex);
    if (!queue->destroying.load(std::memory_order_relaxed) &&
        open_sl_queue_is_registered(queue)) {
        queue->buffers.clear();
        queue->played_frames = 0;
        queue->buffer_index = 0;
    }
    SDL_UnlockMutex(g_audio_mutex);
}

void set_open_sl_buffer_queue_playing(
    const std::shared_ptr<OpenSlBufferQueue> &queue, bool playing)
{
    if (!queue)
        return;
    std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
    if (g_audio_shutting_down.load(std::memory_order_acquire) ||
        !g_audio_mutex)
        return;
    SDL_LockMutex(g_audio_mutex);
    if (!queue->destroying.load(std::memory_order_relaxed) &&
        open_sl_queue_is_registered(queue))
        queue->playing = playing;
    SDL_UnlockMutex(g_audio_mutex);
}

void set_open_sl_buffer_queue_gain(
    const std::shared_ptr<OpenSlBufferQueue> &queue, float gain)
{
    if (!queue)
        return;
    std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
    if (g_audio_shutting_down.load(std::memory_order_acquire) ||
        !g_audio_mutex)
        return;
    SDL_LockMutex(g_audio_mutex);
    if (!queue->destroying.load(std::memory_order_relaxed) &&
        open_sl_queue_is_registered(queue))
        queue->gain = std::isfinite(gain) ? std::clamp(gain, 0.0f, 4.0f) : 0.0f;
    SDL_UnlockMutex(g_audio_mutex);
}

void set_open_sl_buffer_queue_pan(
    const std::shared_ptr<OpenSlBufferQueue> &queue, int permille)
{
    if (!queue)
        return;
    std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
    if (g_audio_shutting_down.load(std::memory_order_acquire) ||
        !g_audio_mutex)
        return;
    SDL_LockMutex(g_audio_mutex);
    if (!queue->destroying.load(std::memory_order_relaxed) &&
        open_sl_queue_is_registered(queue))
        queue->pan_permille = std::clamp(permille, -1000, 1000);
    SDL_UnlockMutex(g_audio_mutex);
}

uint32_t get_open_sl_buffer_queue_count(
    const std::shared_ptr<OpenSlBufferQueue> &queue)
{
    if (!queue)
        return 0;
    std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
    if (!g_audio_mutex)
        return 0;
    SDL_LockMutex(g_audio_mutex);
    const uint32_t count = static_cast<uint32_t>(std::min<size_t>(
        queue->buffers.size(), UINT32_MAX));
    SDL_UnlockMutex(g_audio_mutex);
    return count;
}

uint32_t get_open_sl_buffer_queue_index(
    const std::shared_ptr<OpenSlBufferQueue> &queue)
{
    if (!queue)
        return 0;
    std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
    if (!g_audio_mutex)
        return 0;
    SDL_LockMutex(g_audio_mutex);
    const uint32_t index = queue->buffer_index;
    SDL_UnlockMutex(g_audio_mutex);
    return index;
}

uint64_t get_open_sl_buffer_queue_position_ms(
    const std::shared_ptr<OpenSlBufferQueue> &queue)
{
    if (!queue)
        return 0;
    std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
    if (!g_audio_mutex)
        return 0;
    SDL_LockMutex(g_audio_mutex);
    const uint64_t position_ms = queue->output_rate > 0
        ? queue->played_frames * 1000u /
              static_cast<uint64_t>(queue->output_rate)
        : 0;
    SDL_UnlockMutex(g_audio_mutex);
    return position_ms;
}

void destroy_open_sl_buffer_queue(
    std::shared_ptr<OpenSlBufferQueue> *queue_pointer)
{
    if (!queue_pointer || !*queue_pointer)
        return;
    const std::shared_ptr<OpenSlBufferQueue> queue = *queue_pointer;
    {
        std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
        if (g_audio_mutex) {
            SDL_LockMutex(g_audio_mutex);
            queue->destroying.store(true, std::memory_order_release);
            queue->playing = false;
            queue->buffers.clear();
            queue->callback = nullptr;
            queue->callback_context = nullptr;
            queue->callback_lifetime.reset();
            for (auto &slot : g_open_sl_queues) {
                if (slot.get() == queue.get())
                    slot.reset();
            }
            SDL_UnlockMutex(g_audio_mutex);
        } else {
            queue->destroying.store(true, std::memory_order_release);
        }
    }

    if (!g_inside_open_sl_completion) {
        std::unique_lock<std::mutex> lock(queue->callback_mutex);
        queue->callback_finished.wait(lock, [&queue] {
            return queue->callbacks_in_flight.load(
                       std::memory_order_acquire) == 0;
        });
    }
    queue_pointer->reset();
}

void shutdown()
{
    std::unique_lock<std::mutex> guard(g_init_mutex);
    if (!g_initialized)
        return;
    if (g_audio_shutting_down.exchange(true, std::memory_order_acq_rel))
        return;

    std::array<std::shared_ptr<OpenSlBufferQueue>, kOpenSlQueueCount>
        closing_queues;
    SDL_AudioDeviceID closing_device = 0;
    {
        std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
        if (g_audio_mutex) {
            SDL_LockMutex(g_audio_mutex);
            for (size_t i = 0; i < g_open_sl_queues.size(); ++i) {
                closing_queues[i] = std::move(g_open_sl_queues[i]);
                if (!closing_queues[i])
                    continue;
                closing_queues[i]->destroying.store(
                    true, std::memory_order_release);
                closing_queues[i]->playing = false;
                closing_queues[i]->buffers.clear();
                closing_queues[i]->callback = nullptr;
                closing_queues[i]->callback_context = nullptr;
                closing_queues[i]->callback_lifetime.reset();
            }
            SDL_UnlockMutex(g_audio_mutex);
        }
        closing_device = g_audio_device;
        g_audio_device = 0;
    }
    guard.unlock();

    if (closing_device)
        SDL_CloseAudioDevice(closing_device);
    for (const auto &queue : closing_queues) {
        if (!queue)
            continue;
        std::unique_lock<std::mutex> lock(queue->callback_mutex);
        queue->callback_finished.wait(lock, [&queue] {
            return queue->callbacks_in_flight.load(
                       std::memory_order_acquire) == 0;
        });
    }

    guard.lock();
    {
        std::lock_guard<std::mutex> api_lock(g_open_sl_api_mutex);
        if (g_audio_mutex) {
            SDL_LockMutex(g_audio_mutex);
            g_song = Voice{};
            for (Voice &voice : g_voices)
                voice = Voice{};
            for (SoundSample &sample : g_sounds)
                sample = SoundSample{};
            SDL_UnlockMutex(g_audio_mutex);
            SDL_DestroyMutex(g_audio_mutex);
            g_audio_mutex = nullptr;
        }
    }
    if (g_mpg123_initialized) {
        mpg123_exit();
        g_mpg123_initialized = false;
    }
    g_initialized = false;
}

} // namespace open_citadel::audio
