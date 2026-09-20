#include <cmath>
#include <cstdio>

#include "../../vr/locomotion.h"

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

    SnapTurnLatch snap;
    CHECK(near(snap.update(0.2f, 45.0f), 0.0f));
    CHECK(near(snap.update(0.8f, 45.0f), 45.0f));
    CHECK(snap.latched());
    CHECK(near(snap.update(1.0f, 45.0f), 0.0f));
    CHECK(near(snap.update(0.4f, 45.0f), 0.0f));
    CHECK(near(snap.update(0.2f, 45.0f), 0.0f));
    CHECK(!snap.latched());
    CHECK(near(snap.update(-0.9f, 30.0f), -30.0f));
    snap.reset();
    CHECK(!snap.latched());

    const Vec3 forward = smooth_move_delta(
        Stick2{0.0f, 1.0f}, 0.0f, 2.0f, 0.5f);
    CHECK(near(forward.x, 0.0f));
    CHECK(near(forward.y, 0.0f));
    CHECK(near(forward.z, -1.0f));

    const Vec3 yawed = smooth_move_delta(
        Stick2{0.0f, 1.0f}, degrees_to_radians(90.0f), 2.0f, 0.5f);
    CHECK(near(yawed.x, -1.0f));
    CHECK(near(yawed.z, 0.0f));

    const Vec3 diagonal = smooth_move_delta(
        Stick2{1.0f, 1.0f}, 0.0f, 1.0f, 1.0f);
    CHECK(near(std::sqrt(diagonal.x * diagonal.x +
                         diagonal.z * diagonal.z), 1.0f));

    const Vec3 invalid = smooth_move_delta(
        Stick2{0.0f, 1.0f}, 0.0f, -1.0f, 1.0f);
    CHECK(near(invalid.x, 0.0f));
    CHECK(near(invalid.y, 0.0f));
    CHECK(near(invalid.z, 0.0f));

    return 0;
}
