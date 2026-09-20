#pragma once

#include <algorithm>

namespace open_citadel {

inline float scale_position(float position, int source_extent,
                            int target_extent) noexcept
{
    if (source_extent <= 0 || target_extent <= 0)
        return 0.0f;
    const float scaled = position * static_cast<float>(target_extent) /
                         static_cast<float>(source_extent);
    return std::clamp(scaled, 0.0f,
                      static_cast<float>(target_extent - 1));
}

inline float scale_delta(float delta, int source_extent,
                         int target_extent) noexcept
{
    if (source_extent <= 0 || target_extent <= 0)
        return 0.0f;
    return delta * static_cast<float>(target_extent) /
           static_cast<float>(source_extent);
}

} // namespace open_citadel
