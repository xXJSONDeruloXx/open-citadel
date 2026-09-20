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
        "uncap_fps=true\n"
        "uncapped_benchmark=true\n"
        "mouse_sensitivity=9.0\n"
        "invert_mouse_y=true\n"
        "vr_enabled=true\n"
        "vr_world_scale=1.25\n"
        "vr_render_scale=9.0\n"
        "vr_near_clip_m=0.001\n"
        "vr_far_clip_m=9000\n"
        "vr_snap_turn_degrees=5\n"
        "vr_smooth_move_mps=2.5\n"
        "vr_smooth_turn=true\n"
        "vr_vignette=false\n"
        "vr_seated=true\n"
        "vr_preferred_refresh_hz=500\n"
        "move_forward=Up\n"
        "move_backward=Down\n"
        "move_left=Left\n"
        "move_right=Right\n"
        "unknown_option=ignored\n"
        "width=not-a-number\n"
        "height=200\n");
    open_citadel::read_user_settings(input, &settings);
    CHECK(settings.width == 1920);
    CHECK(settings.height == 240);
    CHECK(settings.fullscreen);
    CHECK(!settings.vsync);
    CHECK(settings.uncap_fps);
    CHECK(settings.uncapped_benchmark);
    CHECK(settings.mouse_sensitivity == 4.0f);
    CHECK(settings.invert_mouse_y);
    CHECK(settings.vr_enabled);
    CHECK(settings.vr_world_scale == 1.25f);
    CHECK(settings.vr_render_scale == 2.0f);
    CHECK(settings.vr_near_clip_m == 0.01f);
    CHECK(settings.vr_far_clip_m == 5000.0f);
    CHECK(settings.vr_snap_turn_degrees == 15.0f);
    CHECK(settings.vr_smooth_move_mps == 2.5f);
    CHECK(settings.vr_smooth_turn);
    CHECK(!settings.vr_vignette);
    CHECK(settings.vr_seated);
    CHECK(settings.vr_preferred_refresh_hz == 240);
    CHECK(settings.move_forward == "Up");
    CHECK(settings.move_backward == "Down");
    CHECK(settings.move_left == "Left");
    CHECK(settings.move_right == "Right");

    std::ostringstream serialized;
    open_citadel::write_user_settings(serialized, settings);
    open_citadel::UserSettings round_trip;
    std::istringstream serialized_input(serialized.str());
    open_citadel::read_user_settings(serialized_input, &round_trip);
    CHECK(round_trip.width == settings.width);
    CHECK(round_trip.height == settings.height);
    CHECK(round_trip.fullscreen == settings.fullscreen);
    CHECK(round_trip.vsync == settings.vsync);
    CHECK(round_trip.uncap_fps == settings.uncap_fps);
    CHECK(round_trip.uncapped_benchmark == settings.uncapped_benchmark);
    CHECK(round_trip.mouse_sensitivity == settings.mouse_sensitivity);
    CHECK(round_trip.invert_mouse_y == settings.invert_mouse_y);
    CHECK(round_trip.vr_enabled == settings.vr_enabled);
    CHECK(round_trip.vr_world_scale == settings.vr_world_scale);
    CHECK(round_trip.vr_render_scale == settings.vr_render_scale);
    CHECK(round_trip.vr_near_clip_m == settings.vr_near_clip_m);
    CHECK(round_trip.vr_far_clip_m == settings.vr_far_clip_m);
    CHECK(round_trip.vr_snap_turn_degrees == settings.vr_snap_turn_degrees);
    CHECK(round_trip.vr_smooth_move_mps == settings.vr_smooth_move_mps);
    CHECK(round_trip.vr_smooth_turn == settings.vr_smooth_turn);
    CHECK(round_trip.vr_vignette == settings.vr_vignette);
    CHECK(round_trip.vr_seated == settings.vr_seated);
    CHECK(round_trip.vr_preferred_refresh_hz == settings.vr_preferred_refresh_hz);
    CHECK(round_trip.move_forward == settings.move_forward);
    CHECK(round_trip.move_backward == settings.move_backward);
    CHECK(round_trip.move_left == settings.move_left);
    CHECK(round_trip.move_right == settings.move_right);

    CHECK(!open_citadel::parse_settings_boolean("yes", &round_trip.vsync));
    CHECK(open_citadel::parse_settings_boolean("false", &round_trip.vsync));
    CHECK(!round_trip.vsync);
    std::string key_name = "unchanged";
    CHECK(open_citadel::parse_settings_key_name(" Page Down ", &key_name));
    CHECK(key_name == "Page Down");
    CHECK(open_citadel::parse_settings_key_name("=", &key_name));
    CHECK(key_name == "=");
    CHECK(open_citadel::parse_settings_key_name("#", &key_name));
    CHECK(key_name == "#");
    CHECK(!open_citadel::parse_settings_key_name(
        std::string(33, 'x'), &key_name));
    open_citadel::UserSettings symbolic_key;
    symbolic_key.move_forward = "=";
    symbolic_key.move_backward = "#";
    std::ostringstream symbolic_serialized;
    open_citadel::write_user_settings(symbolic_serialized, symbolic_key);
    open_citadel::UserSettings symbolic_round_trip;
    std::istringstream symbolic_input(symbolic_serialized.str());
    open_citadel::read_user_settings(symbolic_input, &symbolic_round_trip);
    CHECK(symbolic_round_trip.move_forward == "=");
    CHECK(symbolic_round_trip.move_backward == "#");
    return 0;
}
