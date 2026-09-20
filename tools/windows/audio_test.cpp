#include <cmath>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "citadel_audio.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;

static std::string g_game_dir;

const char *io_game_dir(void)
{
    return g_game_dir.c_str();
}

static void put_u16(std::ofstream &file, uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
    };
    file.write(bytes, sizeof(bytes));
}

static void put_u32(std::ofstream &file, uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    file.write(bytes, sizeof(bytes));
}

static bool write_test_wav(const fs::path &path)
{
    constexpr uint32_t sample_rate = 44100;
    constexpr uint16_t channels = 2;
    constexpr uint16_t bits_per_sample = 16;
    constexpr uint32_t frame_count = 512;
    constexpr uint32_t data_bytes = frame_count * channels * sizeof(int16_t);

    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;
    file.write("RIFF", 4);
    put_u32(file, 36 + data_bytes);
    file.write("WAVEfmt ", 8);
    put_u32(file, 16);
    put_u16(file, 1);
    put_u16(file, channels);
    put_u32(file, sample_rate);
    put_u32(file, sample_rate * channels * bits_per_sample / 8);
    put_u16(file, channels * bits_per_sample / 8);
    put_u16(file, bits_per_sample);
    file.write("data", 4);
    put_u32(file, data_bytes);

    for (uint32_t i = 0; i < frame_count; ++i) {
        const double phase = 2.0 * 3.14159265358979323846 * i / 64.0;
        const int16_t sample = static_cast<int16_t>(std::sin(phase) * 12000.0);
        put_u16(file, static_cast<uint16_t>(sample));
        put_u16(file, static_cast<uint16_t>(sample));
    }
    return file.good();
}

int main(int argc, char **argv)
{
    SDL_SetMainReady();
    if (SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) != 0) {
        std::cerr << "could not select SDL dummy audio driver\n";
        return 1;
    }

    const fs::path test_game_dir = fs::temp_directory_path() /
        ("open-citadel-audio-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    g_game_dir = test_game_dir.string();
    std::error_code error;
    fs::create_directories(test_game_dir, error);
    const fs::path wav_path = test_game_dir / "tone.wav";
    if (error || !write_test_wav(wav_path)) {
        std::cerr << "could not create audio test WAV\n";
        return 1;
    }

    int result = 0;
    const int sound_id = open_citadel::audio::load_sound("tone.wav");
    if (sound_id <= 0) {
        std::cerr << "WAV sound load failed\n";
        result = 1;
        goto cleanup;
    }
    if (open_citadel::audio::load_sound("appbundle:/assets/tone.wav") !=
        sound_id) {
        std::cerr << "asset URI sound resolution failed\n";
        result = 1;
        goto cleanup;
    }

    {
        const int stream_id = open_citadel::audio::play_sound(sound_id, true);
        if (stream_id <= 1000) {
            std::cerr << "sound playback did not allocate a stream\n";
            result = 1;
            goto cleanup;
        }
        open_citadel::audio::set_volume(stream_id, 0.5f);
        open_citadel::audio::stop_sound(stream_id);
    }

#if defined(_WIN32)
    {
        const int descriptor = _open(wav_path.string().c_str(),
                                     _O_RDONLY | _O_BINARY);
        if (descriptor < 0) {
            std::cerr << "could not open audio test WAV descriptor\n";
            result = 1;
            goto cleanup;
        }
        open_citadel::audio::play_song(
            descriptor, 0, static_cast<int64_t>(fs::file_size(wav_path)),
            "tone.wav");
        _close(descriptor);
        open_citadel::audio::stop_song();
    }

    if (argc > 1) {
        const fs::path donor_audio_path = fs::absolute(fs::u8path(argv[1]));
        std::string extension = donor_audio_path.extension().string();
        for (char &value : extension)
            value = static_cast<char>(std::tolower(
                static_cast<unsigned char>(value)));

        if (extension == ".wav") {
            const fs::path game_root = donor_audio_path.parent_path()
                                           .parent_path().parent_path();
            g_game_dir = game_root.string();
            const std::string relative =
                fs::relative(donor_audio_path, game_root).generic_string();
            const int donor_sound_id =
                open_citadel::audio::load_sound(relative.c_str());
            if (donor_sound_id <= 0) {
                std::cerr << "could not decode optional donor WAV\n";
                result = 1;
                goto cleanup;
            }
            const int donor_stream_id =
                open_citadel::audio::play_sound(donor_sound_id, false);
            if (donor_stream_id <= 1000) {
                std::cerr << "could not play optional donor WAV\n";
                result = 1;
            }
            open_citadel::audio::stop_sound(donor_stream_id);
            open_citadel::audio::unload_sound(donor_sound_id);
        } else {
            const int descriptor = _open(donor_audio_path.string().c_str(),
                                         _O_RDONLY | _O_BINARY);
            if (descriptor < 0) {
                std::cerr << "could not open optional donor MP3\n";
                result = 1;
                goto cleanup;
            }
            open_citadel::audio::play_song(
                descriptor, 0,
                static_cast<int64_t>(fs::file_size(donor_audio_path)),
                argv[1]);
            _close(descriptor);
            open_citadel::audio::stop_song();
        }
    }
#endif

    if (open_citadel::audio::load_sound("../tone.wav") != -1) {
        std::cerr << "audio path traversal was not rejected\n";
        result = 1;
    }

cleanup:
    if (sound_id > 0)
        open_citadel::audio::unload_sound(sound_id);
    open_citadel::audio::shutdown();
    SDL_Quit();
    fs::remove_all(test_game_dir, error);
    return result;
}
