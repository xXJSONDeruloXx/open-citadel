#include <cstdio>
#include <sstream>

#include "settings.h"

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
    open_citadel::UserSettings settings;
    std::istringstream input(
        "# comments are accepted\n"
        " width = 1920 \n"
        "height=1080\n"
        "fullscreen=ON\n"
        "vsync=off\n"
        "mouse_sensitivity=9.0\n"
        "invert_mouse_y=true\n"
        "unknown_option=ignored\n"
        "width=not-a-number\n"
        "height=200\n");
    open_citadel::read_user_settings(input, &settings);
    CHECK(settings.width == 1920);
    CHECK(settings.height == 240);
    CHECK(settings.fullscreen);
    CHECK(!settings.vsync);
    CHECK(settings.mouse_sensitivity == 4.0f);
    CHECK(settings.invert_mouse_y);

    std::ostringstream serialized;
    open_citadel::write_user_settings(serialized, settings);
    open_citadel::UserSettings round_trip;
    std::istringstream serialized_input(serialized.str());
    open_citadel::read_user_settings(serialized_input, &round_trip);
    CHECK(round_trip.width == settings.width);
    CHECK(round_trip.height == settings.height);
    CHECK(round_trip.fullscreen == settings.fullscreen);
    CHECK(round_trip.vsync == settings.vsync);
    CHECK(round_trip.mouse_sensitivity == settings.mouse_sensitivity);
    CHECK(round_trip.invert_mouse_y == settings.invert_mouse_y);

    CHECK(!open_citadel::parse_settings_boolean("yes", &round_trip.vsync));
    CHECK(open_citadel::parse_settings_boolean("false", &round_trip.vsync));
    CHECK(!round_trip.vsync);
    return 0;
}
