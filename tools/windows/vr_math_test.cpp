#include <cmath>
#include <cstdio>

#include "../../vr/vr_math.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "check failed at %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static bool near(float a, float b, float epsilon = 0.0005f)
{
    return std::fabs(a - b) <= epsilon;
}

int main()
{
    using namespace open_citadel::vr;

    const Pose identity{};
    const Pose child{{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 2.0f, 3.0f}};
    const Pose composed_identity = compose(identity, child);
    CHECK(near(composed_identity.position.x, 1.0f));
    CHECK(near(composed_identity.position.y, 2.0f));
    CHECK(near(composed_identity.position.z, 3.0f));

    constexpr float pi = 3.14159265358979323846f;
    const float half_yaw = pi * 0.25f;
    const Pose yaw90{{0.0f, std::sin(half_yaw), 0.0f, std::cos(half_yaw)},
                     {0.0f, 0.0f, 0.0f}};
    const Vec3 forward{0.0f, 0.0f, -1.0f};
    const Vec3 rotated = rotate(yaw90.orientation, forward);
    CHECK(near(rotated.x, -1.0f));
    CHECK(near(rotated.y, 0.0f));
    CHECK(near(rotated.z, 0.0f));

    const Pose head{{0.0f, 0.0f, 0.0f, 1.0f}, {2.0f, 1.6f, -4.0f}};
    const Pose left_eye{{0.0f, 0.0f, 0.0f, 1.0f}, {-0.032f, 0.0f, 0.0f}};
    const Pose right_eye{{0.0f, 0.0f, 0.0f, 1.0f}, {0.032f, 0.0f, 0.0f}};
    const Pose world_left = compose(head, left_eye);
    const Pose world_right = compose(head, right_eye);
    CHECK(near(world_left.position.x, 1.968f));
    CHECK(near(world_right.position.x, 2.032f));
    CHECK(near(world_right.position.x - world_left.position.x, 0.064f));

    const Pose round_trip = compose(head, compose(inverse(head), child));
    CHECK(near(round_trip.position.x, child.position.x));
    CHECK(near(round_trip.position.y, child.position.y));
    CHECK(near(round_trip.position.z, child.position.z));

    Fov left_fov{-0.85f, 0.75f, 0.80f, -0.78f};
    Fov right_fov{-0.75f, 0.87f, 0.79f, -0.81f};
    const Fov united = union_fov(left_fov, right_fov);
    CHECK(near(united.angle_left, -0.85f));
    CHECK(near(united.angle_right, 0.87f));
    CHECK(near(united.angle_up, 0.80f));
    CHECK(near(united.angle_down, -0.81f));

    Mat4 projection{};
    CHECK(projection_gles(left_fov, 0.05f, 500.0f, &projection));
    CHECK(is_finite(projection));
    CHECK(projection.at(0, 0) > 0.0f);
    CHECK(projection.at(1, 1) > 0.0f);
    CHECK(near(projection.at(3, 2), -1.0f));
    CHECK(!near(projection.at(0, 2), 0.0f)); // asymmetric FOV
    CHECK(!projection_gles(left_fov, 0.0f, 500.0f, &projection));
    CHECK(!projection_gles(left_fov, 1.0f, 0.5f, &projection));

    const Mat4 view = view_matrix_from_pose(head);
    CHECK(is_finite(view));
    CHECK(near(view.at(0, 3), -2.0f));
    CHECK(near(view.at(1, 3), -1.6f));
    CHECK(near(view.at(2, 3), 4.0f));

    return 0;
}
