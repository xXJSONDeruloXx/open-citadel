#include "window_geometry.h"

#include <cmath>
#include <iostream>

int main()
{
    int failures = 0;
    const auto check = [&failures](const char *name, float actual,
                                   float expected) {
        if (std::fabs(actual - expected) <= 0.001f)
            return;
        std::cerr << name << ": expected " << expected << ", got "
                  << actual << '\n';
        ++failures;
    };

    check("upscaled point", open_citadel::scale_position(320.0f, 640, 1280),
          640.0f);
    check("downscaled point", open_citadel::scale_position(960.0f, 1920, 960),
          480.0f);
    check("negative point clamp",
          open_citadel::scale_position(-5.0f, 640, 1280), 0.0f);
    check("upper point clamp",
          open_citadel::scale_position(640.0f, 640, 1280), 1279.0f);
    check("invalid point extent",
          open_citadel::scale_position(10.0f, 0, 1280), 0.0f);
    check("scaled drag delta", open_citadel::scale_delta(4.0f, 1280, 2560),
          8.0f);
    check("invalid drag extent",
          open_citadel::scale_delta(4.0f, 0, 2560), 0.0f);

    return failures ? 1 : 0;
}
