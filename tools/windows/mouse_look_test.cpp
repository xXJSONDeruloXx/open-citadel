#include "mouse_look.h"

#include <cmath>
#include <cstdio>
#include <limits>

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
    auto axes = open_citadel::relative_mouse_look_axes(8.0f, 4.0f, 1.0f,
                                                       false);
    CHECK(std::fabs(axes.x - 0.5f) < 0.001f);
    CHECK(std::fabs(axes.y - 0.25f) < 0.001f);

    axes = open_citadel::relative_mouse_look_axes(-32.0f, 32.0f, 1.0f,
                                                  false);
    CHECK(axes.x == -1.0f);
    CHECK(axes.y == 1.0f);

    axes = open_citadel::relative_mouse_look_axes(0.0f, 8.0f, 1.0f, true);
    CHECK(axes.x == 0.0f);
    CHECK(axes.y == -0.5f);

    axes = open_citadel::relative_mouse_look_axes(16.0f, 0.0f, 0.01f,
                                                  false);
    CHECK(std::fabs(axes.x - 0.1f) < 0.001f);

    axes = open_citadel::relative_mouse_look_axes(
        std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f, false);
    CHECK(axes.x == 0.0f && axes.y == 0.0f);
    return 0;
}
