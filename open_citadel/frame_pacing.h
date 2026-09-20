#pragma once

namespace open_citadel {

struct FramePacingMode {
    bool disable_game_fps_cap = false;
    bool vsync_enabled = true;
};

inline FramePacingMode resolve_frame_pacing(bool vsync_preference,
                                             bool uncap_fps,
                                             bool uncapped_benchmark)
{
    return {
        uncap_fps || uncapped_benchmark,
        vsync_preference && !uncapped_benchmark,
    };
}

} // namespace open_citadel
