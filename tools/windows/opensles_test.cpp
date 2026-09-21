#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

#include <SDL2/SDL.h>

#include "citadel_audio.h"
#include "opensles_abi.h"
#include "opensles_compat.h"

#if defined(_MSC_VER)
#define OPENSL_TEST_CALL __cdecl
#else
#define OPENSL_TEST_CALL
#endif

const char *io_game_dir(void)
{
    return ".";
}

namespace {

struct CompletionContext {
    std::vector<int16_t> samples;
    std::atomic<unsigned int> completed{0};
    std::atomic<SLresult> requeue_result{SL_RESULT_PRECONDITIONS_VIOLATED};
    std::atomic<bool> requeued{false};
};

void OPENSL_TEST_CALL queue_completed(SLBufferQueueItf queue, void *opaque)
{
    auto *context = static_cast<CompletionContext *>(opaque);
    if (!context)
        return;
    const unsigned int completed =
        context->completed.fetch_add(1, std::memory_order_relaxed) + 1;
    if (completed == 1 && !context->requeued.exchange(
                              true, std::memory_order_relaxed)) {
        context->requeue_result.store(
            (*queue)->Enqueue(queue, context->samples.data(),
                              static_cast<SLuint32>(context->samples.size() *
                                                    sizeof(int16_t))),
            std::memory_order_release);
    }
}

bool require(bool condition, const char *what)
{
    if (condition)
        return true;
    std::cerr << "OpenSLES compatibility test failed: " << what << '\n';
    return false;
}

bool run_queue_test()
{
    SLObjectItf engine = nullptr;
    if (!require(slCreateEngine(&engine, 0, nullptr, 0, nullptr, nullptr) ==
                     SL_RESULT_SUCCESS,
                 "create engine"))
        return false;
    if (!require((*engine)->Realize(engine, SL_BOOLEAN_FALSE) ==
                     SL_RESULT_SUCCESS,
                 "realize engine"))
        return false;

    SLEngineItf engine_interface = nullptr;
    if (!require((*engine)->GetInterface(engine, SL_IID_ENGINE,
                                         &engine_interface) ==
                     SL_RESULT_SUCCESS && engine_interface,
                 "get engine interface"))
        return false;

    SLObjectItf output_mix = nullptr;
    if (!require((*engine_interface)->CreateOutputMix(
                     engine_interface, &output_mix, 0, nullptr, nullptr) ==
                     SL_RESULT_SUCCESS && output_mix,
                 "create output mix"))
        return false;
    if (!require((*output_mix)->Realize(output_mix, SL_BOOLEAN_FALSE) ==
                     SL_RESULT_SUCCESS,
                 "realize output mix"))
        return false;

    SLDataLocator_AndroidSimpleBufferQueue queue_locator{
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 3};
    SLDataFormat_PCM format{
        SL_DATAFORMAT_PCM,
        1,
        22050000,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_SPEAKER_FRONT_CENTER,
        SL_BYTEORDER_LITTLEENDIAN,
    };
    SLDataSource source{&queue_locator, &format};
    SLDataLocator_OutputMix mix_locator{SL_DATALOCATOR_OUTPUTMIX, output_mix};
    SLDataSink sink{&mix_locator, nullptr};
    const SLInterfaceID requested[] = {
        SL_IID_PLAY,
        SL_IID_VOLUME,
        SL_IID_BUFFERQUEUE,
    };
    const SLboolean required[] = {
        SL_BOOLEAN_TRUE,
        SL_BOOLEAN_TRUE,
        SL_BOOLEAN_TRUE,
    };

    SLObjectItf player = nullptr;
    if (!require((*engine_interface)->CreateAudioPlayer(
                     engine_interface, &player, &source, &sink,
                     static_cast<SLuint32>(std::size(requested)), requested,
                     required) == SL_RESULT_SUCCESS && player,
                 "create PCM player"))
        return false;
    if (!require((*player)->Realize(player, SL_BOOLEAN_FALSE) ==
                     SL_RESULT_SUCCESS,
                 "realize PCM player"))
        return false;

    SLPlayItf play = nullptr;
    SLVolumeItf volume = nullptr;
    SLBufferQueueItf queue = nullptr;
    if (!require((*player)->GetInterface(player, SL_IID_PLAY, &play) ==
                     SL_RESULT_SUCCESS && play,
                 "get play interface") ||
        !require((*player)->GetInterface(player, SL_IID_VOLUME, &volume) ==
                     SL_RESULT_SUCCESS && volume,
                 "get volume interface") ||
        !require((*player)->GetInterface(player, SL_IID_BUFFERQUEUE, &queue) ==
                     SL_RESULT_SUCCESS && queue,
                 "get buffer queue interface"))
        return false;

    SLmillibel level = 0;
    SLboolean muted = SL_BOOLEAN_FALSE;
    if (!require((*volume)->SetVolumeLevel(volume, -600) == SL_RESULT_SUCCESS &&
                     (*volume)->GetVolumeLevel(volume, &level) ==
                         SL_RESULT_SUCCESS && level == -600,
                 "set/get volume") ||
        !require((*volume)->SetMute(volume, SL_BOOLEAN_TRUE) ==
                     SL_RESULT_SUCCESS &&
                     (*volume)->GetMute(volume, &muted) == SL_RESULT_SUCCESS &&
                     muted == SL_BOOLEAN_TRUE,
                 "set/get mute"))
        return false;
    if (!require((*volume)->EnableStereoPosition(volume, SL_BOOLEAN_TRUE) ==
                     SL_RESULT_SUCCESS &&
                     (*volume)->SetStereoPosition(volume, -250) ==
                         SL_RESULT_SUCCESS,
                 "stereo pan"))
        return false;

    CompletionContext context;
    context.samples.resize(2205);
    for (size_t i = 0; i < context.samples.size(); ++i)
        context.samples[i] = static_cast<int16_t>(
            static_cast<int>(i % 64) * 256 - 8192);
    if (!require((*queue)->RegisterCallback(queue, &queue_completed,
                                            &context) == SL_RESULT_SUCCESS,
                 "register completion callback") ||
        !require((*queue)->Enqueue(queue, context.samples.data(),
                                   static_cast<SLuint32>(
                                       context.samples.size() *
                                       sizeof(int16_t))) == SL_RESULT_SUCCESS,
                 "enqueue first PCM buffer") ||
        !require((*play)->SetPlayState(play, SL_PLAYSTATE_PLAYING) ==
                     SL_RESULT_SUCCESS,
                 "start playback"))
        return false;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(3);
    while (context.completed.load(std::memory_order_acquire) < 2 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!require(context.completed.load(std::memory_order_acquire) == 2,
                 "both queued buffers completed") ||
        !require(context.requeue_result.load(std::memory_order_acquire) ==
                     SL_RESULT_SUCCESS,
                 "re-enqueue from completion callback"))
        return false;

    SLBufferQueueState state{};
    SLmillisecond position = 0;
    SLuint32 play_state = SL_PLAYSTATE_STOPPED;
    if (!require((*queue)->GetState(queue, &state) == SL_RESULT_SUCCESS &&
                     state.count == 0 && state.playIndex == 2,
                 "buffer queue state") ||
        !require((*play)->GetPosition(play, &position) == SL_RESULT_SUCCESS &&
                     position >= 100,
                 "playback position") ||
        !require((*play)->GetPlayState(play, &play_state) ==
                     SL_RESULT_SUCCESS &&
                     play_state == SL_PLAYSTATE_PLAYING,
                 "play state"))
        return false;

    if (!require((*queue)->Enqueue(queue, context.samples.data(),
                                   static_cast<SLuint32>(
                                       context.samples.size() *
                                       sizeof(int16_t))) == SL_RESULT_SUCCESS &&
                     (*queue)->Clear(queue) == SL_RESULT_SUCCESS &&
                     (*queue)->GetState(queue, &state) == SL_RESULT_SUCCESS &&
                     state.count == 0,
                 "clear pending PCM"))
        return false;

    (*play)->SetPlayState(play, SL_PLAYSTATE_STOPPED);
    (*player)->Destroy(player);
    (*output_mix)->Destroy(output_mix);
    (*engine)->Destroy(engine);
    return true;
}

} // namespace

int main()
{
    SDL_SetMainReady();
    if (SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) != 0) {
        std::cerr << "could not select SDL dummy audio driver\n";
        return 1;
    }

    const bool okay = run_queue_test();
    open_citadel::opensles::shutdown();
    open_citadel::audio::shutdown();
    SDL_Quit();
    if (!okay)
        return 1;
    std::cout << "OpenSLES PCM queue compatibility passed\n";
    return 0;
}
