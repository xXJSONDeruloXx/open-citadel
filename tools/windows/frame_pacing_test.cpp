#include "frame_pacing.h"

#include <cstdio>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "check failed at %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

int main()
{
    const auto defaults = open_citadel::resolve_frame_pacing(true, false,
                                                              false);
    CHECK(!defaults.disable_game_fps_cap);
    CHECK(defaults.vsync_enabled);

    // Removing UE3's 60 FPS cap is independent of the display sync setting.
    const auto uncapped_game = open_citadel::resolve_frame_pacing(
        true, true, false);
    CHECK(uncapped_game.disable_game_fps_cap);
    CHECK(uncapped_game.vsync_enabled);

    // A benchmark run must remove both limits without changing the preference.
    const auto benchmark = open_citadel::resolve_frame_pacing(true, false,
                                                               true);
    CHECK(benchmark.disable_game_fps_cap);
    CHECK(!benchmark.vsync_enabled);

    const auto benchmark_with_vsync_preference_off =
        open_citadel::resolve_frame_pacing(false, false, true);
    CHECK(benchmark_with_vsync_preference_off.disable_game_fps_cap);
    CHECK(!benchmark_with_vsync_preference_off.vsync_enabled);
    return 0;
}
