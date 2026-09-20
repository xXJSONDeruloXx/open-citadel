#pragma once

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

namespace open_citadel {

struct UserSettings {
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
    bool uncap_fps = false;
    bool uncapped_benchmark = false;
    float mouse_sensitivity = 1.0f;
    bool invert_mouse_y = false;
    bool vr_enabled = false;
    float vr_world_scale = 1.0f;
    float vr_render_scale = 1.0f;
    float vr_near_clip_m = 0.05f;
    float vr_far_clip_m = 500.0f;
    float vr_snap_turn_degrees = 45.0f;
    float vr_smooth_move_mps = 1.4f;
    bool vr_smooth_turn = false;
    bool vr_vignette = true;
    bool vr_seated = false;
    int vr_preferred_refresh_hz = 0;
    std::string move_forward = "w";
    std::string move_backward = "s";
    std::string move_left = "a";
    std::string move_right = "d";
};

inline std::string_view trim_settings_value(std::string_view value)
{
    constexpr std::string_view whitespace = " \t\r\n";
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return {};
    const std::size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

inline bool parse_settings_integer(std::string_view value, int minimum,
                                   int maximum, int *result)
{
    if (!result || value.empty())
        return false;
    const std::string text(value);
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno || end == text.c_str() || !end || *end)
        return false;
    *result = static_cast<int>(std::clamp<long>(parsed, minimum, maximum));
    return true;
}

inline bool parse_settings_float(std::string_view value, float minimum,
                                 float maximum, float *result)
{
    if (!result || value.empty())
        return false;
    const std::string text(value);
    char *end = nullptr;
    errno = 0;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno || end == text.c_str() || !end || *end || !std::isfinite(parsed))
        return false;
    *result = std::clamp(parsed, minimum, maximum);
    return true;
}

inline bool settings_text_equals(std::string_view value,
                                 std::string_view expected)
{
    if (value.size() != expected.size())
        return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        char ch = value[i];
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
        if (ch != expected[i])
            return false;
    }
    return true;
}

inline bool parse_settings_boolean(std::string_view value, bool *result)
{
    if (!result)
        return false;
    if (value == "1" || settings_text_equals(value, "true") ||
        settings_text_equals(value, "on")) {
        *result = true;
        return true;
    }
    if (value == "0" || settings_text_equals(value, "false") ||
        settings_text_equals(value, "off")) {
        *result = false;
        return true;
    }
    return false;
}

inline bool parse_settings_key_name(std::string_view value,
                                    std::string *result)
{
    if (!result)
        return false;
    value = trim_settings_value(value);
    if (value.empty() || value.size() > 32)
        return false;
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch > 0x7e)
            return false;
    }
    result->assign(value);
    return true;
}

inline void read_user_settings(std::istream &input, UserSettings *settings)
{
    if (!settings)
        return;

    std::string line;
    while (std::getline(input, line)) {
        const std::string_view trimmed = trim_settings_value(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';')
            continue;
        const std::size_t separator = trimmed.find('=');
        if (separator == std::string_view::npos)
            continue;

        const std::string_view key =
            trim_settings_value(trimmed.substr(0, separator));
        const std::string_view value =
            trim_settings_value(trimmed.substr(separator + 1));
        if (key == "width") {
            parse_settings_integer(value, 320, 7680, &settings->width);
        } else if (key == "height") {
            parse_settings_integer(value, 240, 4320, &settings->height);
        } else if (key == "fullscreen") {
            parse_settings_boolean(value, &settings->fullscreen);
        } else if (key == "vsync") {
            parse_settings_boolean(value, &settings->vsync);
        } else if (key == "uncap_fps") {
            parse_settings_boolean(value, &settings->uncap_fps);
        } else if (key == "uncapped_benchmark") {
            parse_settings_boolean(value, &settings->uncapped_benchmark);
        } else if (key == "mouse_sensitivity") {
            parse_settings_float(value, 0.1f, 4.0f,
                                &settings->mouse_sensitivity);
        } else if (key == "invert_mouse_y") {
            parse_settings_boolean(value, &settings->invert_mouse_y);
        } else if (key == "vr_enabled") {
            parse_settings_boolean(value, &settings->vr_enabled);
        } else if (key == "vr_world_scale") {
            parse_settings_float(value, 0.1f, 10.0f, &settings->vr_world_scale);
        } else if (key == "vr_render_scale") {
            parse_settings_float(value, 0.25f, 2.0f, &settings->vr_render_scale);
        } else if (key == "vr_near_clip_m") {
            parse_settings_float(value, 0.01f, 1.0f, &settings->vr_near_clip_m);
        } else if (key == "vr_far_clip_m") {
            parse_settings_float(value, 10.0f, 5000.0f, &settings->vr_far_clip_m);
        } else if (key == "vr_snap_turn_degrees") {
            parse_settings_float(value, 15.0f, 90.0f,
                                 &settings->vr_snap_turn_degrees);
        } else if (key == "vr_smooth_move_mps") {
            parse_settings_float(value, 0.1f, 10.0f,
                                 &settings->vr_smooth_move_mps);
        } else if (key == "vr_smooth_turn") {
            parse_settings_boolean(value, &settings->vr_smooth_turn);
        } else if (key == "vr_vignette") {
            parse_settings_boolean(value, &settings->vr_vignette);
        } else if (key == "vr_seated") {
            parse_settings_boolean(value, &settings->vr_seated);
        } else if (key == "vr_preferred_refresh_hz") {
            parse_settings_integer(value, 0, 240,
                                   &settings->vr_preferred_refresh_hz);
        } else if (key == "move_forward") {
            parse_settings_key_name(value, &settings->move_forward);
        } else if (key == "move_backward") {
            parse_settings_key_name(value, &settings->move_backward);
        } else if (key == "move_left") {
            parse_settings_key_name(value, &settings->move_left);
        } else if (key == "move_right") {
            parse_settings_key_name(value, &settings->move_right);
        }
    }
}

inline void write_user_settings(std::ostream &output,
                                const UserSettings &settings)
{
    output << "# Open Citadel settings. Environment variables override these "
              "values.\n"
           << "# Window dimensions and fullscreen mode apply after restart.\n"
           << "width=" << settings.width << '\n'
           << "height=" << settings.height << '\n'
           << "fullscreen=" << (settings.fullscreen ? "true" : "false") << '\n'
           << "vsync=" << (settings.vsync ? "true" : "false") << '\n'
           << "uncap_fps=" << (settings.uncap_fps ? "true" : "false")
           << '\n'
           << "uncapped_benchmark="
           << (settings.uncapped_benchmark ? "true" : "false") << '\n'
           << "mouse_sensitivity=" << std::fixed << std::setprecision(2)
           << settings.mouse_sensitivity << '\n'
           << "invert_mouse_y=" << (settings.invert_mouse_y ? "true" : "false")
           << '\n'
           << "# VR is opt-in. Refresh 0 means let the runtime choose.\n"
           << "vr_enabled=" << (settings.vr_enabled ? "true" : "false") << '\n'
           << "vr_world_scale=" << settings.vr_world_scale << '\n'
           << "vr_render_scale=" << settings.vr_render_scale << '\n'
           << "vr_near_clip_m=" << settings.vr_near_clip_m << '\n'
           << "vr_far_clip_m=" << settings.vr_far_clip_m << '\n'
           << "vr_snap_turn_degrees=" << settings.vr_snap_turn_degrees << '\n'
           << "vr_smooth_move_mps=" << settings.vr_smooth_move_mps << '\n'
           << "vr_smooth_turn=" << (settings.vr_smooth_turn ? "true" : "false")
           << '\n'
           << "vr_vignette=" << (settings.vr_vignette ? "true" : "false") << '\n'
           << "vr_seated=" << (settings.vr_seated ? "true" : "false") << '\n'
           << "vr_preferred_refresh_hz=" << settings.vr_preferred_refresh_hz
           << '\n'
           << "move_forward=" << settings.move_forward << '\n'
           << "move_backward=" << settings.move_backward << '\n'
           << "move_left=" << settings.move_left << '\n'
           << "move_right=" << settings.move_right << '\n';
}

} // namespace open_citadel
