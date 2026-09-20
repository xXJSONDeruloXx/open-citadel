#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace open_citadel::vr {

// OpenXR uses a right-handed coordinate system with +Y up and -Z forward.
// These types deliberately do not depend on OpenXR headers so pose/projection
// math can be regression-tested on machines without an XR runtime.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Pose {
    Quat orientation{};
    Vec3 position{};
};

struct Fov {
    float angle_left = 0.0f;
    float angle_right = 0.0f;
    float angle_up = 0.0f;
    float angle_down = 0.0f;
};

struct Mat4 {
    // Column-major, matching OpenGL/OpenGL ES convention.
    std::array<float, 16> value{};

    float &at(std::size_t row, std::size_t column)
    {
        return value[column * 4 + row];
    }

    const float &at(std::size_t row, std::size_t column) const
    {
        return value[column * 4 + row];
    }

    static Mat4 identity()
    {
        Mat4 matrix{};
        matrix.at(0, 0) = 1.0f;
        matrix.at(1, 1) = 1.0f;
        matrix.at(2, 2) = 1.0f;
        matrix.at(3, 3) = 1.0f;
        return matrix;
    }
};

inline Vec3 add(const Vec3 &a, const Vec3 &b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 subtract(const Vec3 &a, const Vec3 &b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 scale(const Vec3 &v, float factor)
{
    return {v.x * factor, v.y * factor, v.z * factor};
}

inline float quat_length_squared(const Quat &q)
{
    return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
}

inline Quat normalize(const Quat &q)
{
    const float length_squared = quat_length_squared(q);
    if (!(length_squared > 0.0f) || !std::isfinite(length_squared))
        return {};
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {q.x * inverse_length, q.y * inverse_length,
            q.z * inverse_length, q.w * inverse_length};
}

inline Quat conjugate(const Quat &q)
{
    return {-q.x, -q.y, -q.z, q.w};
}

inline Quat multiply(const Quat &a, const Quat &b)
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

inline Vec3 rotate(const Quat &rotation, const Vec3 &v)
{
    const Quat q = normalize(rotation);
    const Quat point{v.x, v.y, v.z, 0.0f};
    const Quat rotated = multiply(multiply(q, point), conjugate(q));
    return {rotated.x, rotated.y, rotated.z};
}

// Composition is parent * child: child is expressed in the parent's space.
inline Pose compose(const Pose &parent, const Pose &child)
{
    Pose result{};
    result.orientation = normalize(multiply(parent.orientation, child.orientation));
    result.position = add(parent.position, rotate(parent.orientation, child.position));
    return result;
}

inline Pose inverse(const Pose &pose)
{
    Pose result{};
    result.orientation = normalize(conjugate(normalize(pose.orientation)));
    result.position = rotate(result.orientation, scale(pose.position, -1.0f));
    return result;
}

inline Mat4 matrix_from_pose(const Pose &pose)
{
    const Quat q = normalize(pose.orientation);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    Mat4 result = Mat4::identity();
    result.at(0, 0) = 1.0f - 2.0f * (yy + zz);
    result.at(1, 0) = 2.0f * (xy + wz);
    result.at(2, 0) = 2.0f * (xz - wy);

    result.at(0, 1) = 2.0f * (xy - wz);
    result.at(1, 1) = 1.0f - 2.0f * (xx + zz);
    result.at(2, 1) = 2.0f * (yz + wx);

    result.at(0, 2) = 2.0f * (xz + wy);
    result.at(1, 2) = 2.0f * (yz - wx);
    result.at(2, 2) = 1.0f - 2.0f * (xx + yy);

    result.at(0, 3) = pose.position.x;
    result.at(1, 3) = pose.position.y;
    result.at(2, 3) = pose.position.z;
    return result;
}

inline Mat4 view_matrix_from_pose(const Pose &world_from_view)
{
    return matrix_from_pose(inverse(world_from_view));
}

// Construct an asymmetric OpenGL ES projection matrix using the OpenXR FOV
// angles. OpenGL ES NDC depth is [-1, +1].
inline bool projection_gles(const Fov &fov, float near_z, float far_z,
                            Mat4 *out)
{
    if (!out || !(near_z > 0.0f) || !(far_z > near_z) ||
        !std::isfinite(near_z) || !std::isfinite(far_z))
        return false;

    const float tan_left = std::tan(fov.angle_left);
    const float tan_right = std::tan(fov.angle_right);
    const float tan_down = std::tan(fov.angle_down);
    const float tan_up = std::tan(fov.angle_up);
    if (!std::isfinite(tan_left) || !std::isfinite(tan_right) ||
        !std::isfinite(tan_down) || !std::isfinite(tan_up) ||
        !(tan_right > tan_left) || !(tan_up > tan_down))
        return false;

    const float left = tan_left * near_z;
    const float right = tan_right * near_z;
    const float bottom = tan_down * near_z;
    const float top = tan_up * near_z;

    Mat4 matrix{};
    matrix.at(0, 0) = (2.0f * near_z) / (right - left);
    matrix.at(1, 1) = (2.0f * near_z) / (top - bottom);
    matrix.at(0, 2) = (right + left) / (right - left);
    matrix.at(1, 2) = (top + bottom) / (top - bottom);
    matrix.at(2, 2) = -(far_z + near_z) / (far_z - near_z);
    matrix.at(2, 3) = -(2.0f * far_z * near_z) / (far_z - near_z);
    matrix.at(3, 2) = -1.0f;
    *out = matrix;
    return true;
}

// A conservative angular union useful for initial center-eye culling work.
// Positional eye offsets can require additional widening at very close near
// planes; hardware validation must still perform doorway/wall lean tests.
inline Fov union_fov(const Fov &left, const Fov &right)
{
    return {
        std::min(left.angle_left, right.angle_left),
        std::max(left.angle_right, right.angle_right),
        std::max(left.angle_up, right.angle_up),
        std::min(left.angle_down, right.angle_down),
    };
}

inline bool is_finite(const Mat4 &matrix)
{
    for (float value : matrix.value) {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

} // namespace open_citadel::vr
