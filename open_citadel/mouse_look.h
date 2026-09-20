#pragma once

#include <algorithm>
#include <cmath>

namespace open_citadel {

struct RelativeMouseAxes {
    float x;
    float y;
};

inline RelativeMouseAxes relative_mouse_look_axes(
    float delta_x, float delta_y, float sensitivity, bool invert_y) noexcept
{
    if (!std::isfinite(delta_x) || !std::isfinite(delta_y) ||
        !std::isfinite(sensitivity))
        return {0.0f, 0.0f};

    const float gain = std::clamp(sensitivity, 0.1f, 4.0f) / 16.0f;
    return {
        std::clamp(delta_x * gain, -1.0f, 1.0f),
        std::clamp(delta_y * gain * (invert_y ? -1.0f : 1.0f), -1.0f, 1.0f),
    };
}

} // namespace open_citadel
