#pragma once

#include <algorithm>
#include <cmath>

#include "vr_math.h"

namespace open_citadel::vr {

struct Stick2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Produces at most one snap per deliberate stick deflection. The stick must
// return below release_threshold before another snap can fire.
class SnapTurnLatch {
public:
    explicit SnapTurnLatch(float activation_threshold = 0.70f,
                           float release_threshold = 0.35f)
        : activation_(std::clamp(activation_threshold, 0.1f, 1.0f)),
          release_(std::clamp(release_threshold, 0.0f, activation_))
    {
    }

    float update(float axis_x, float snap_degrees)
    {
        if (!std::isfinite(axis_x) || !std::isfinite(snap_degrees))
            return 0.0f;

        if (latched_) {
            if (std::fabs(axis_x) <= release_)
                latched_ = false;
            return 0.0f;
        }

        if (axis_x >= activation_) {
            latched_ = true;
            return std::fabs(snap_degrees);
        }
        if (axis_x <= -activation_) {
            latched_ = true;
            return -std::fabs(snap_degrees);
        }
        return 0.0f;
    }

    void reset() { latched_ = false; }
    bool latched() const { return latched_; }

private:
    float activation_;
    float release_;
    bool latched_ = false;
};

// Converts a left-stick sample into a world-space displacement for an
// artificial-locomotion body transform. It never changes the physical HMD
// pose. +stick.y is forward, +stick.x is right. yaw_radians rotates around +Y.
inline Vec3 smooth_move_delta(Stick2 stick, float body_yaw_radians,
                              float speed_mps, float dt_seconds)
{
    if (!std::isfinite(stick.x) || !std::isfinite(stick.y) ||
        !std::isfinite(body_yaw_radians) || !std::isfinite(speed_mps) ||
        !std::isfinite(dt_seconds) || speed_mps <= 0.0f ||
        dt_seconds <= 0.0f)
        return {};

    const float magnitude = std::sqrt(stick.x * stick.x + stick.y * stick.y);
    if (magnitude > 1.0f && magnitude > 0.0f) {
        stick.x /= magnitude;
        stick.y /= magnitude;
    }

    const Vec3 local{
        stick.x * speed_mps * dt_seconds,
        0.0f,
        -stick.y * speed_mps * dt_seconds,
    };
    const float half_yaw = body_yaw_radians * 0.5f;
    const Quat yaw{
        0.0f,
        std::sin(half_yaw),
        0.0f,
        std::cos(half_yaw),
    };
    return rotate(yaw, local);
}

inline float degrees_to_radians(float degrees)
{
    constexpr float pi = 3.14159265358979323846f;
    return degrees * (pi / 180.0f);
}

} // namespace open_citadel::vr
