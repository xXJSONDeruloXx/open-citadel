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
    float resolution_scale = 1.0f;
    bool native_mouse_look = true;
    bool invert_mouse_y = false;
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

inline float normalize_resolution_scale(float scale) noexcept
{
    if (!std::isfinite(scale))
        return 1.0f;
    scale = std::clamp(scale, 0.5f, 1.0f);
    if (scale < 0.6f)
        return 0.5f;
    if (scale < 0.8f)
        return 0.75f;
    return 1.0f;
}

inline bool parse_settings_resolution_scale(std::string_view value,
                                            float *result)
{
    float parsed = 1.0f;
    if (!result || !parse_settings_float(value, 0.5f, 1.0f, &parsed))
        return false;
    *result = normalize_resolution_scale(parsed);
    return true;
}

inline int resolution_scale_option(float scale) noexcept
{
    const float normalized = normalize_resolution_scale(scale);
    return normalized < 0.6f ? 0 : normalized < 0.8f ? 1 : 2;
}

inline float resolution_scale_from_option(int option) noexcept
{
    switch (option) {
    case 0: return 0.5f;
    case 1: return 0.75f;
    default: return 1.0f;
    }
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
        } else if (key == "resolution_scale") {
            parse_settings_resolution_scale(value,
                                            &settings->resolution_scale);
        } else if (key == "native_mouse_look") {
            parse_settings_boolean(value, &settings->native_mouse_look);
        } else if (key == "invert_mouse_y") {
            parse_settings_boolean(value, &settings->invert_mouse_y);
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
    output << "# Open Citadel desktop settings. Environment variables override "
              "these values.\n"
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
           << "resolution_scale=" << settings.resolution_scale << '\n'
           << "native_mouse_look="
           << (settings.native_mouse_look ? "true" : "false") << '\n'
           << "invert_mouse_y=" << (settings.invert_mouse_y ? "true" : "false")
           << '\n'
           << "move_forward=" << settings.move_forward << '\n'
           << "move_backward=" << settings.move_backward << '\n'
           << "move_left=" << settings.move_left << '\n'
           << "move_right=" << settings.move_right << '\n';
}

} // namespace open_citadel
