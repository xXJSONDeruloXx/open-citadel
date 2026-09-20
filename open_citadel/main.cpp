#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/sysinfo.h>
#endif

#include <SDL2/SDL.h>

#include "io_util.h"
#include "fix_path.h"
#include "so_util.h"
#include "khronos/gles2.h"
#include "jni.h"
#include "classes/ue3_java_app.h"
#include "classes/citadel_audio.h"
#include "trace.h"
#include "crash.h"
#include "gles2_probe.h"
#include "android_input_codes.h"
#include "keyboard_controls.h"
#include "settings.h"

extern "C" void android_egl_init(SDL_Window *window, SDL_GLContext gl);

static so_module *g_module = nullptr;

/* Existing bionic pthread compatibility uses this accessor to determine
 * whether a pthread entry point belongs to the mapped Android module. Keep the
 * ABI while the shared loader is being generalized beyond Katamari. */
so_module *katamari_module(void)
{
    return g_module;
}

static int report_unresolved_symbols(so_module *mod)
{
    int missing = 0;
    for (int i = 0; i < mod->num_dynsym; ++i) {
        Elf_Sym *sym = &mod->dynsym[i];
        if (sym->st_shndx != SHN_UNDEF)
            continue;
        const char *name = mod->dynstr + sym->st_name;
        if (!name || !*name || so_resolve_link(mod, name))
            continue;
        if (ELF32_ST_BIND(sym->st_info) == STB_WEAK)
            continue;
        fprintf(stderr, "unresolved symbol: %s\n", name);
        ++missing;
    }
    return missing;
}

extern "C" int so_after_relocate(so_module *mod)
{
    g_module = mod;

    /* Called before the loader executes .init_array, so constructor faults are
     * reported with guest-relative PCs instead of disappearing into a bare
     * SIGSEGV. */
    crash_report_init(mod, "libUnrealEngine3.so");

    const int missing = report_unresolved_symbols(mod);
    if (missing)
        fprintf(stderr, "OpenCitadel: %d unresolved import(s)\n", missing);
    else
        fprintf(stderr, "OpenCitadel: all native imports resolved\n");
    return missing ? 1 : 0;
}

static int env_int(const char *name, int fallback, int minimum, int maximum)
{
    const char *value = getenv(name);
    if (!value || !*value)
        return fallback;
    char *end = nullptr;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno || !end || end == value || *end)
        return fallback;
    return (int)std::clamp<long>(parsed, minimum, maximum);
}

static float env_float(const char *name, float fallback,
                       float minimum, float maximum)
{
    const char *value = getenv(name);
    if (!value || !*value)
        return fallback;
    char *end = nullptr;
    const float parsed = strtof(value, &end);
    if (end == value || !end || *end || !std::isfinite(parsed))
        return fallback;
    return std::clamp(parsed, minimum, maximum);
}

static bool env_bool(const char *name, bool fallback)
{
    const char *value = getenv(name);
    if (!value || !*value)
        return fallback;
    if (SDL_strcasecmp(value, "1") == 0 ||
        SDL_strcasecmp(value, "true") == 0 ||
        SDL_strcasecmp(value, "on") == 0)
        return true;
    if (SDL_strcasecmp(value, "0") == 0 ||
        SDL_strcasecmp(value, "false") == 0 ||
        SDL_strcasecmp(value, "off") == 0)
        return false;
    return fallback;
}

static std::filesystem::path user_settings_path()
{
    const char *override_path = getenv("OPEN_CITADEL_CONFIG");
    if (override_path && *override_path)
        return std::filesystem::u8path(override_path);

    char *preference_directory = SDL_GetPrefPath("OpenCitadel", "EpicCitadel");
    if (!preference_directory) {
        fprintf(stderr, "OpenCitadel: SDL_GetPrefPath: %s\n", SDL_GetError());
        return {};
    }
    const std::filesystem::path path =
        std::filesystem::u8path(preference_directory) / "settings.ini";
    SDL_free(preference_directory);
    return path;
}

static bool load_user_settings(const std::filesystem::path &path,
                               open_citadel::UserSettings *settings)
{
    if (path.empty() || !settings)
        return false;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    open_citadel::UserSettings loaded = *settings;
    open_citadel::read_user_settings(input, &loaded);
    if (input.bad())
        return false;
    *settings = loaded;
    return true;
}

static bool save_user_settings(const std::filesystem::path &path,
                               const open_citadel::UserSettings &settings)
{
    if (path.empty())
        return false;

    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            fprintf(stderr, "OpenCitadel: cannot create settings directory: %s\n",
                    error.message().c_str());
            return false;
        }
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary,
                             std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output) {
            fprintf(stderr, "OpenCitadel: cannot write settings file %s\n",
                    path.string().c_str());
            return false;
        }
        open_citadel::write_user_settings(output, settings);
        output.flush();
        if (!output) {
            fprintf(stderr, "OpenCitadel: failed writing settings file %s\n",
                    path.string().c_str());
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        output.close();
        if (!output) {
            fprintf(stderr, "OpenCitadel: failed closing settings file %s\n",
                    path.string().c_str());
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        error = std::error_code((int)GetLastError(), std::system_category());
#else
    std::filesystem::rename(temporary, path, error);
#endif
    if (error) {
        fprintf(stderr, "OpenCitadel: cannot replace settings file %s: %s\n",
                path.string().c_str(), error.message().c_str());
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

static void show_settings_dialog(SDL_Window *window,
                                const std::filesystem::path &settings_path,
                                open_citadel::UserSettings *settings,
                                open_citadel::UserSettings *saved_settings)
{
    if (!window || !settings || !saved_settings)
        return;

    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT |
             SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Done"},
        {0, 1, "Sensitivity -"},
        {0, 2, "Sensitivity +"},
        {0, 3, "Toggle Y inversion"},
        {0, 4, "Toggle VSync"},
        {0, 5, "Reset mouse"},
    };

    for (;;) {
        char message[768];
        std::snprintf(
            message, sizeof(message),
            "Mouse sensitivity: %.1f\nVertical look: %s\nVSync: %s\n\n"
            "Window: %d x %d (%s)\n"
            "Window size and fullscreen apply after restarting.\n"
            "Settings file: %s",
            settings->mouse_sensitivity,
            settings->invert_mouse_y ? "inverted" : "normal",
            settings->vsync ? "on" : "off", settings->width,
            settings->height, settings->fullscreen ? "fullscreen" : "windowed",
            settings_path.empty() ? "unavailable" : settings_path.string().c_str());
        const SDL_MessageBoxData data = {
            SDL_MESSAGEBOX_INFORMATION,
            window,
            "Epic Citadel settings",
            message,
            (int)(sizeof(buttons) / sizeof(buttons[0])),
            buttons,
            nullptr,
        };
        int pressed = 0;
        if (SDL_ShowMessageBox(&data, &pressed) != 0) {
            fprintf(stderr, "OpenCitadel: settings dialog failed: %s\n",
                    SDL_GetError());
            return;
        }

        bool changed = false;
        switch (pressed) {
        case 1:
            settings->mouse_sensitivity = std::max(
                0.1f, std::round((settings->mouse_sensitivity - 0.1f) * 10.0f) /
                          10.0f);
            changed = true;
            break;
        case 2:
            settings->mouse_sensitivity = std::min(
                4.0f, std::round((settings->mouse_sensitivity + 0.1f) * 10.0f) /
                          10.0f);
            changed = true;
            break;
        case 3:
            settings->invert_mouse_y = !settings->invert_mouse_y;
            changed = true;
            break;
        case 4:
            settings->vsync = !settings->vsync;
            if (SDL_GL_SetSwapInterval(settings->vsync ? 1 : 0) != 0)
                fprintf(stderr, "OpenCitadel: swap interval change failed: %s\n",
                        SDL_GetError());
            changed = true;
            break;
        case 5:
            settings->mouse_sensitivity = 1.0f;
            settings->invert_mouse_y = false;
            changed = true;
            break;
        default:
            return;
        }

        if (changed) {
            saved_settings->vsync = settings->vsync;
            saved_settings->mouse_sensitivity = settings->mouse_sensitivity;
            saved_settings->invert_mouse_y = settings->invert_mouse_y;
            if (!save_user_settings(settings_path, *saved_settings))
                fprintf(stderr,
                        "OpenCitadel: settings are active but were not saved\n");
        }
    }
}

static bool file_is_present(const std::filesystem::path &path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

static std::string default_game_dir(const char *argv0, const char *abi_dir)
{
    std::error_code error;
    std::filesystem::path cwd = std::filesystem::current_path(error);
    if (error)
        cwd = ".";

    error.clear();
    std::filesystem::path executable = std::filesystem::absolute(argv0, error);
    if (error)
        executable = cwd / argv0;
    const std::filesystem::path executable_dir = executable.parent_path();

    const std::filesystem::path candidates[] = {
        cwd / "gamedata" / "epic-citadel-1.07",
        executable_dir / "gamedata" / "epic-citadel-1.07",
        executable_dir / ".." / ".." / ".." / "gamedata" /
            "epic-citadel-1.07",
    };
    for (const auto &candidate : candidates) {
        error.clear();
        const std::filesystem::path resolved =
            std::filesystem::weakly_canonical(candidate, error);
        const std::filesystem::path game_dir = error
            ? candidate.lexically_normal() : resolved;
        if (file_is_present(game_dir / abi_dir / "libUnrealEngine3.so") &&
            file_is_present(game_dir / "obb" /
                            "main.903107.com.epicgames.EpicCitadel.obb"))
            return game_dir.string();
    }
    return {};
}

static SDL_Window *create_window(int width, int height, bool fullscreen,
                                 SDL_GLContext *out_gl)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL;
#if !defined(_WIN32)
    flags |= SDL_WINDOW_RESIZABLE;
#else
    /* UE3 keeps its startup viewport dimensions; changing the SDL surface
     * after initialization currently leaves the guest rendering letterboxed. */
#endif
    if (fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (getenv("OPEN_CITADEL_HIDDEN"))
        flags |= SDL_WINDOW_HIDDEN;

    SDL_Window *window = SDL_CreateWindow(
        "Open Citadel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, flags);
    if (!window)
        return nullptr;

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        SDL_DestroyWindow(window);
        return nullptr;
    }
    *out_gl = gl;
    return window;
}

static void toggle_fullscreen(SDL_Window *window)
{
#if defined(_WIN32)
    static bool already_notified = false;
    if (already_notified)
        return;
    already_notified = true;
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION, "Epic Citadel display mode",
        "The Windows build selects resolution and fullscreen at startup. "
        "Set OPEN_CITADEL_WIDTH and OPEN_CITADEL_HEIGHT for windowed size, "
        "or OPEN_CITADEL_FULLSCREEN=1 for fullscreen. Live mode changes "
        "are not available yet.", window);
#else
    const bool fullscreen =
        (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (getenv("OPEN_CITADEL_TRACE_INPUT"))
        fprintf(stderr, "OpenCitadel: fullscreen toggle -> %s\n",
                fullscreen ? "windowed" : "desktop");
    if (SDL_SetWindowFullscreen(window, fullscreen ? 0 :
                                SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
        fprintf(stderr, "OpenCitadel: fullscreen toggle failed: %s\n",
                SDL_GetError());
#endif
}

template <typename T>
static T guest_symbol(so_module *mod, const char *name)
{
    uintptr_t value = so_symbol(mod, name);
    if (!value)
        fprintf(stderr, "OpenCitadel: missing guest export %s\n", name);
    return reinterpret_cast<T>(value);
}


static int android_keycode(SDL_Keycode key)
{
    if (key >= SDLK_0 && key <= SDLK_9)
        return android_input::kKey0 + (int)(key - SDLK_0);
    if (key >= SDLK_a && key <= SDLK_z)
        return android_input::kKeyA + (int)(key - SDLK_a);
    if (key >= SDLK_KP_0 && key <= SDLK_KP_9)
        return android_input::kKeyNumpad0 + (int)(key - SDLK_KP_0);
    if (key >= SDLK_F1 && key <= SDLK_F12)
        return android_input::kKeyF1 + (int)(key - SDLK_F1);

    switch (key) {
    case SDLK_HOME: return android_input::kKeyHome;
    case SDLK_ESCAPE: return android_input::kKeyEscape;
    case SDLK_LEFT: return android_input::kKeyDpadLeft;
    case SDLK_RIGHT: return android_input::kKeyDpadRight;
    case SDLK_UP: return android_input::kKeyDpadUp;
    case SDLK_DOWN: return android_input::kKeyDpadDown;
    case SDLK_PAGEUP: return android_input::kKeyPageUp;
    case SDLK_PAGEDOWN: return android_input::kKeyPageDown;
    case SDLK_END: return android_input::kKeyMoveEnd;
    case SDLK_INSERT: return android_input::kKeyInsert;
    case SDLK_TAB: return android_input::kKeyTab;
    case SDLK_SPACE: return android_input::kKeySpace;
    case SDLK_RETURN: return android_input::kKeyEnter;
    case SDLK_KP_ENTER: return android_input::kKeyNumpadEnter;
    case SDLK_BACKSPACE:
    case SDLK_DELETE: return android_input::kKeyDel;
    case SDLK_BACKQUOTE: return android_input::kKeyGrave;
    case SDLK_MINUS: return android_input::kKeyMinus;
    case SDLK_KP_MINUS: return android_input::kKeyNumpadSubtract;
    case SDLK_EQUALS: return android_input::kKeyEquals;
    case SDLK_KP_EQUALS: return android_input::kKeyNumpadEquals;
    case SDLK_LEFTBRACKET: return android_input::kKeyLeftBracket;
    case SDLK_RIGHTBRACKET: return android_input::kKeyRightBracket;
    case SDLK_BACKSLASH: return android_input::kKeyBackslash;
    case SDLK_SEMICOLON: return android_input::kKeySemicolon;
    case SDLK_QUOTE: return android_input::kKeyApostrophe;
    case SDLK_SLASH: return android_input::kKeySlash;
    case SDLK_KP_DIVIDE: return android_input::kKeyNumpadDivide;
    case SDLK_KP_MULTIPLY: return android_input::kKeyNumpadMultiply;
    case SDLK_KP_PLUS: return android_input::kKeyNumpadAdd;
    case SDLK_KP_PERIOD: return android_input::kKeyNumpadDot;
    case SDLK_KP_COMMA: return android_input::kKeyNumpadComma;
    case SDLK_MENU: return android_input::kKeyMenu;
    case SDLK_LALT: return android_input::kKeyAltLeft;
    case SDLK_RALT: return android_input::kKeyAltRight;
    case SDLK_LSHIFT: return android_input::kKeyShiftLeft;
    case SDLK_RSHIFT: return android_input::kKeyShiftRight;
    case SDLK_LCTRL: return android_input::kKeyCtrlLeft;
    case SDLK_RCTRL: return android_input::kKeyCtrlRight;
    case SDLK_CAPSLOCK: return android_input::kKeyCapsLock;
    case SDLK_SCROLLLOCK: return android_input::kKeyScrollLock;
    case SDLK_NUMLOCKCLEAR: return android_input::kKeyNumLock;
    case SDLK_PRINTSCREEN: return android_input::kKeySysRq;
    case SDLK_PAUSE: return android_input::kKeyBreak;
    case SDLK_LGUI: return android_input::kKeyMetaLeft;
    case SDLK_RGUI: return android_input::kKeyMetaRight;
    case SDLK_COMMA: return android_input::kKeyComma;
    case SDLK_PERIOD: return android_input::kKeyPeriod;
    default: return 0;
    }
}

static int android_gamepad_key(SDL_GameControllerButton button)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return android_input::kKeyButtonA;
    case SDL_CONTROLLER_BUTTON_B: return android_input::kKeyButtonB;
    case SDL_CONTROLLER_BUTTON_X: return android_input::kKeyButtonX;
    case SDL_CONTROLLER_BUTTON_Y: return android_input::kKeyButtonY;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return android_input::kKeyButtonL1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return android_input::kKeyButtonR1;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return android_input::kKeyButtonThumbL;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return android_input::kKeyButtonThumbR;
    case SDL_CONTROLLER_BUTTON_START: return android_input::kKeyButtonStart;
    case SDL_CONTROLLER_BUTTON_BACK: return android_input::kKeyButtonSelect;
    case SDL_CONTROLLER_BUTTON_GUIDE: return android_input::kKeyButtonMode;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return android_input::kKeyDpadUp;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return android_input::kKeyDpadDown;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return android_input::kKeyDpadLeft;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return android_input::kKeyDpadRight;
    default: return 0;
    }
}

static int android_axis(SDL_GameControllerAxis axis)
{
    switch (axis) {
    case SDL_CONTROLLER_AXIS_LEFTX: return android_input::kAxisX;
    case SDL_CONTROLLER_AXIS_LEFTY: return android_input::kAxisY;
    case SDL_CONTROLLER_AXIS_RIGHTX: return android_input::kAxisZ;
    case SDL_CONTROLLER_AXIS_RIGHTY: return android_input::kAxisRz;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT: return android_input::kAxisLtrigger;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return android_input::kAxisRtrigger;
    default: return -1;
    }
}

static float android_axis_value(SDL_GameControllerAxis axis, Sint16 raw)
{
    if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
        axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        return std::clamp((float)raw / 32767.0f, 0.0f, 1.0f);
    return std::clamp((float)raw / 32767.0f, -1.0f, 1.0f);
}

static int unicode_for_key(const SDL_KeyboardEvent &event)
{
    SDL_Keycode key = event.keysym.sym;
    if (key >= SDLK_a && key <= SDLK_z) {
        int ch = 'a' + (int)(key - SDLK_a);
        if (event.keysym.mod & KMOD_SHIFT)
            ch -= 'a' - 'A';
        return ch;
    }
    if (key >= SDLK_0 && key <= SDLK_9)
        return '0' + (int)(key - SDLK_0);
    if (key >= 0x20 && key <= 0x7e)
        return (int)key;
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        fprintf(stderr, "usage: %s [imported-open-citadel-directory]\n", argv[0]);
        fprintf(stderr,
                "If omitted, searches ./gamedata/epic-citadel-1.07 and "
                "the matching path beside the executable.\n"
                "OPEN_CITADEL_GAME_DIR may also select the data directory.\n"
                "F1 shows controls; F2 opens saved mouse and VSync settings.\n"
                "Preferences are stored in %%APPDATA%%\\OpenCitadel\\EpicCitadel "
                "or OPEN_CITADEL_CONFIG.\n"
                "OPEN_CITADEL_WIDTH/HEIGHT choose startup window size; "
                "OPEN_CITADEL_FULLSCREEN=1 starts fullscreen.\n"
                "OPEN_CITADEL_MOUSE_SENSITIVITY scales drag-look; "
                "OPEN_CITADEL_INVERT_MOUSE_Y=1 flips vertical drag-look; "
                "OPEN_CITADEL_VSYNC=0 disables VSync.\n");
        if (argc == 2)
            return 0;
        return 2;
    }

#if defined(__i386__)
    const char *abi_dir = "lib/x86";
#elif defined(__arm__)
    const char *abi_dir = "lib/armeabi-v7a";
#else
#error Open Citadel bring-up currently supports i386 and ARMv7
#endif

    const char *requested_game_dir = argc == 2
        ? argv[1] : getenv("OPEN_CITADEL_GAME_DIR");
    std::string game_dir_storage = requested_game_dir
        ? requested_game_dir : default_game_dir(argv[0], abi_dir);
    if (game_dir_storage.empty()) {
        fprintf(stderr,
                "OpenCitadel: no imported Epic Citadel 1.07 data found; "
                "pass its directory or set OPEN_CITADEL_GAME_DIR\n");
        return 2;
    }
    const char *game_dir = game_dir_storage.c_str();
    fprintf(stderr, "OpenCitadel: game data=%s\n", game_dir);

    char lib_dir[PATH_MAX];
    char lib_path[PATH_MAX];
    char main_obb[PATH_MAX];
    snprintf(lib_dir, sizeof(lib_dir), "%s/%s", game_dir, abi_dir);
    snprintf(lib_path, sizeof(lib_path), "%s/libUnrealEngine3.so", lib_dir);
    snprintf(main_obb, sizeof(main_obb),
             "%s/obb/main.903107.com.epicgames.EpicCitadel.obb", game_dir);

    if (!file_is_present(lib_path)) {
        fprintf(stderr, "OpenCitadel: missing engine %s\n", lib_path);
        return 2;
    }
    if (!file_is_present(main_obb)) {
        fprintf(stderr, "OpenCitadel: missing donor OBB %s\n", main_obb);
        return 2;
    }

    io_set_game_dir(game_dir);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "OpenCitadel: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    const std::filesystem::path settings_path = user_settings_path();
    open_citadel::UserSettings saved_settings;
    if (load_user_settings(settings_path, &saved_settings)) {
        fprintf(stderr, "OpenCitadel: settings=%s\n",
                settings_path.string().c_str());
    } else if (!settings_path.empty()) {
        std::error_code settings_error;
        const bool exists = std::filesystem::exists(settings_path, settings_error);
        if (!exists && !settings_error) {
            if (save_user_settings(settings_path, saved_settings))
                fprintf(stderr, "OpenCitadel: created settings=%s\n",
                        settings_path.string().c_str());
        } else {
            fprintf(stderr, "OpenCitadel: could not read settings=%s%s%s\n",
                    settings_path.string().c_str(),
                    settings_error ? ": " : "",
                    settings_error ? settings_error.message().c_str() : "");
        }
    }

    open_citadel::UserSettings settings = saved_settings;
    settings.width = env_int("OPEN_CITADEL_WIDTH", settings.width, 320, 7680);
    settings.height = env_int("OPEN_CITADEL_HEIGHT", settings.height, 240, 4320);
    settings.fullscreen = env_bool("OPEN_CITADEL_FULLSCREEN", settings.fullscreen);
    settings.vsync = env_bool("OPEN_CITADEL_VSYNC", settings.vsync);
    settings.mouse_sensitivity = env_float(
        "OPEN_CITADEL_MOUSE_SENSITIVITY", settings.mouse_sensitivity, 0.1f, 4.0f);
    settings.invert_mouse_y = env_bool(
        "OPEN_CITADEL_INVERT_MOUSE_Y", settings.invert_mouse_y);

    int width = settings.width;
    int height = settings.height;
    SDL_GLContext gl = nullptr;
    SDL_Window *window = create_window(width, height, settings.fullscreen, &gl);
    if (!window) {
        fprintf(stderr, "OpenCitadel: GLES2 context creation failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (settings.fullscreen) {
        SDL_GL_GetDrawableSize(window, &width, &height);
        fprintf(stderr, "OpenCitadel: fullscreen render size=%dx%d\n",
                width, height);
    }

    if (SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0) != 0)
        fprintf(stderr, "OpenCitadel: swap interval unavailable: %s\n",
                SDL_GetError());
    if (getenv("OPEN_CITADEL_TRACE_INPUT")) {
        const Uint32 window_flags = SDL_GetWindowFlags(window);
        fprintf(stderr,
                "OpenCitadel: input-focus=%d keyboard-window=%p window=%p "
                "flags=0x%x\n",
                (window_flags & SDL_WINDOW_INPUT_FOCUS) != 0,
                (void *)SDL_GetKeyboardFocus(), (void *)window, window_flags);
        fprintf(stderr,
                "OpenCitadel: mouse sensitivity=%.2f invert-y=%d\n",
                settings.mouse_sensitivity, settings.invert_mouse_y ? 1 : 0);
    }
    load_gles2_funcs();
    android_egl_init(window, gl);
    open_citadel_java_configure(window, gl, game_dir, main_obb, nullptr);

    const GLubyte *version = glGetString(GL_VERSION);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    fprintf(stderr, "OpenCitadel: GL_VERSION=%s GL_RENDERER=%s\n",
            version ? (const char *)version : "?",
            renderer ? (const char *)renderer : "?");

    JavaVM *vm = nullptr;
    JNIEnv *env = nullptr;
    if (JNI_CreateJavaVM(&vm, &env, nullptr) != JNI_OK || !vm || !env) {
        fprintf(stderr, "OpenCitadel: fake JVM creation failed\n");
        return 1;
    }

    so_set_options(nullptr, lib_dir);
    so_module *mod = so_load_module("libUnrealEngine3.so", nullptr, nullptr);
    if (!mod) {
        fprintf(stderr, "OpenCitadel: failed to map libUnrealEngine3.so\n");
        return 1;
    }
    g_module = mod;

    using OnLoad = jint (*)(JavaVM *, void *);
    OnLoad on_load = guest_symbol<OnLoad>(mod, "JNI_OnLoad");
    if (!on_load)
        return 1;
    jint jni_version = on_load(vm, nullptr);
    fprintf(stderr, "OpenCitadel: JNI_OnLoad -> 0x%x\n", jni_version);
    if (jni_version != JNI_VERSION_1_4 && jni_version != JNI_VERSION_1_6) {
        fprintf(stderr, "OpenCitadel: JNI_OnLoad failed\n");
        return 1;
    }

    using InitEGL = jboolean (*)(JNIEnv *, jobject);
    using Initialize = jboolean (*)(JNIEnv *, jobject, jint, jint, jfloat,
                                    jboolean, jobject, jboolean);
    using Cleanup = void (*)(JNIEnv *, jobject);
    using PostInit = void (*)(JNIEnv *, jobject, jint, jint);
    using Back = void (*)(void);
    using Input = jboolean (*)(JNIEnv *, jobject, jint, jint, jint, jint, jlong);
    using Keyboard = jboolean (*)(JNIEnv *, jobject, jint, jint, jint, jint);
    using JoyAxis = jboolean (*)(JNIEnv *, jobject, jint, jint, jint, jfloat, jlong);
    using JoyButton = jboolean (*)(JNIEnv *, jobject, jint, jint, jint, jlong);
    using Interrupt = jboolean (*)(JNIEnv *, jobject, jboolean);
    using Network = void (*)(JNIEnv *, jobject, jboolean, jboolean);
    using Language = void (*)(JNIEnv *, jobject, jstring);
    using KeyPad = void (*)(JNIEnv *, jobject, jboolean);
    using SystemStats = jboolean (*)(JNIEnv *, jobject, jlong, jfloat);

    InitEGL native_init_egl = guest_symbol<InitEGL>(
        mod, "_Z30NativeCallback_InitEGLCallbackP7_JNIEnvP8_jobject");
    Initialize native_initialize = guest_symbol<Initialize>(
        mod, "_Z25NativeCallback_InitializeP7_JNIEnvP8_jobjectiifhS2_h");
    Cleanup native_cleanup = guest_symbol<Cleanup>(
        mod, "_Z22NativeCallback_CleanupP7_JNIEnvP8_jobject");
    PostInit native_post_init = guest_symbol<PostInit>(
        mod, "_Z29NativeCallback_PostInitUpdateP7_JNIEnvP8_jobjectii");
    Back native_back = guest_symbol<Back>(
        mod, "_Z38NativeCallback_HandleBackButtonPressedv");
    Input native_input = guest_symbol<Input>(
        mod, "_Z25NativeCallback_InputEventP7_JNIEnvP8_jobjectiiiix");
    Keyboard native_keyboard = guest_symbol<Keyboard>(
        mod, "_Z28NativeCallback_KeyboardEventP7_JNIEnvP8_jobjectiiii");
    JoyAxis native_joy_axis = guest_symbol<JoyAxis>(
        mod, "_Z32NativeCallback_JoystickAxisEventP7_JNIEnvP8_jobjectiiifx");
    JoyButton native_joy_button = guest_symbol<JoyButton>(
        mod, "_Z34NativeCallback_JoystickButtonEventP7_JNIEnvP8_jobjectiiix");
    Interrupt native_interrupt = guest_symbol<Interrupt>(
        mod, "_Z34NativeCallback_InterruptionChangedP7_JNIEnvP8_jobjecth");
    Network native_network = guest_symbol<Network>(
        mod, "_Z28NativeCallback_NetworkUpdateP7_JNIEnvP8_jobjecthh");
    Language native_language = guest_symbol<Language>(
        mod, "_Z26NativeCallback_LanguageSetP7_JNIEnvP8_jobjectP8_jstring");
    KeyPad native_keypad = guest_symbol<KeyPad>(
        mod, "_Z27NativeCallback_KeyPadChangeP7_JNIEnvP8_jobjecth");
    SystemStats native_stats = guest_symbol<SystemStats>(
        mod, "_Z26NativeCallback_SystemStatsP7_JNIEnvP8_jobjectxf");

    if (!native_init_egl || !native_initialize || !native_cleanup ||
        !native_post_init || !native_input || !native_keyboard ||
        !native_joy_axis || !native_joy_button || !native_interrupt ||
        !native_network || !native_language || !native_keypad || !native_stats)
        return 1;

    jobject activity = open_citadel_java_activity();

    /* Match UE3JavaApp.systemStartupCheck/PostStartup before the rendering
     * thread is launched. The newer 1.07 donor passes display density as the
     * second SystemStats argument; 1.0 corresponds to Android's baseline
     * density and is overrideable for diagnostics. */
    jlong available_memory = 512LL * 1024LL * 1024LL;
#if defined(_WIN32)
    MEMORYSTATUSEX system_info{};
    system_info.dwLength = sizeof(system_info);
    if (GlobalMemoryStatusEx(&system_info))
        available_memory = (jlong)system_info.ullAvailPhys;
#else
    struct sysinfo system_info {};
    if (sysinfo(&system_info) == 0)
        available_memory = (jlong)system_info.freeram *
                           (jlong)system_info.mem_unit;
#endif
    float density_scale = 1.0f;
    if (const char *density = getenv("OPEN_CITADEL_DENSITY_SCALE"))
        density_scale = std::max(0.1f, (float)atof(density));
    native_stats(env, activity, available_memory, density_scale);
    native_keypad(env, activity, JNI_FALSE);
    native_network(env, activity, JNI_TRUE, JNI_FALSE);
    jstring locale = env->NewStringUTF(
        getenv("OPEN_CITADEL_LANGUAGE") ? getenv("OPEN_CITADEL_LANGUAGE") : "en");
    native_language(env, activity, locale);
    env->DeleteLocalRef(locale);

    if (!native_init_egl(env, activity)) {
        fprintf(stderr, "OpenCitadel: NativeCallback_InitEGLCallback failed\n");
        return 1;
    }
    fprintf(stderr, "OpenCitadel: EGL callback initialized\n");

    /* Android hands the context from the UI thread to UE3GameThread. Do the
     * same explicitly: JavaCallback_makeCurrent will acquire it there. */
    if (SDL_GL_MakeCurrent(window, nullptr) != 0)
        fprintf(stderr, "OpenCitadel: context release warning: %s\n",
                SDL_GetError());

    /* The exported C++ helper carries two legacy parameters that are not
     * present in the Java declaration (IIFZ)Z. The 1.07 implementation only
     * consumes env/activity/width/height, but match its actual native ABI so
     * both i386 cdecl and ARM softfp calls remain well-defined. */
    if (!native_initialize(env, activity, width, height, 1.0f, JNI_FALSE,
                           nullptr, JNI_FALSE)) {
        fprintf(stderr, "OpenCitadel: NativeCallback_Initialize failed\n");
        return 1;
    }
    native_post_init(env, activity, width, height);
    fprintf(stderr, "OpenCitadel: UE3GameThread launched (%dx%d)\n",
            width, height);

    const long frame_limit = getenv("OPEN_CITADEL_FRAME_LIMIT")
        ? atol(getenv("OPEN_CITADEL_FRAME_LIMIT")) : 0;
    const long run_seconds = getenv("OPEN_CITADEL_RUN_SECONDS")
        ? atol(getenv("OPEN_CITADEL_RUN_SECONDS")) : 0;
    const Uint64 start = SDL_GetTicks64();
    long last_reported = -1;
    bool running = true;
    bool interrupted = false;
    bool alt_enter_toggled = false;
    Uint32 mouse_touch_buttons = 0;
    int mouse_x = width / 2;
    int mouse_y = height / 2;
    float touch_x = (float)mouse_x;
    float touch_y = (float)mouse_y;

    SDL_GameController *controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                fprintf(stderr, "OpenCitadel: controller=%s\n",
                        SDL_GameControllerName(controller));
                break;
            }
        }
    }

    open_citadel::KeyboardMovementState keyboard_movement;
    constexpr jint kKeyboardControllerId = 0x40000000;
    constexpr jint kAndroidJoystickDeviceType = 2;
    const auto send_keyboard_movement = [&](jlong timestamp) {
        const open_citadel::MovementAxes axes = keyboard_movement.axes();
        native_joy_axis(env, activity, kKeyboardControllerId,
                        kAndroidJoystickDeviceType, android_input::kAxisX,
                        axes.x, timestamp);
        native_joy_axis(env, activity, kKeyboardControllerId,
                        kAndroidJoystickDeviceType, android_input::kAxisY,
                        axes.y, timestamp);
        if (getenv("OPEN_CITADEL_TRACE_INPUT"))
            fprintf(stderr, "OpenCitadel: WASD virtual stick x=%.2f y=%.2f\n",
                    axes.x, axes.y);
    };

    while (running && !open_citadel_java_shutdown_requested()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const jlong event_time = (jlong)event.common.timestamp;
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                const Uint32 button_mask =
                    event.button.button == SDL_BUTTON_LEFT ? SDL_BUTTON_LMASK :
                    event.button.button == SDL_BUTTON_RIGHT ? SDL_BUTTON_RMASK :
                    0;
                mouse_x = event.button.x;
                mouse_y = event.button.y;
                if (button_mask) {
                    const bool was_active = mouse_touch_buttons != 0;
                    if (event.type == SDL_MOUSEBUTTONDOWN)
                        mouse_touch_buttons |= button_mask;
                    else
                        mouse_touch_buttons &= ~button_mask;

                    if (!was_active && mouse_touch_buttons) {
                        touch_x = (float)mouse_x;
                        touch_y = (float)mouse_y;
                        native_input(env, activity, android_input::kActionDown,
                                     mouse_x, mouse_y, 0, event_time);
                    } else if (was_active && !mouse_touch_buttons) {
                        native_input(env, activity, android_input::kActionUp,
                                     (int)std::lround(touch_x),
                                     (int)std::lround(touch_y), 0, event_time);
                    }
                }
                break;
            }
            case SDL_MOUSEMOTION:
                if (mouse_touch_buttons) {
                    const int delta_x = event.motion.x - mouse_x;
                    const int delta_y = event.motion.y - mouse_y;
                    touch_x = std::clamp(
                        touch_x + delta_x * settings.mouse_sensitivity,
                        0.0f, (float)width - 1.0f);
                    touch_y = std::clamp(
                        touch_y + delta_y * settings.mouse_sensitivity *
                            (settings.invert_mouse_y ? -1.0f : 1.0f),
                        0.0f, (float)height - 1.0f);
                    mouse_x = event.motion.x;
                    mouse_y = event.motion.y;
                    native_input(env, activity, android_input::kActionMove,
                                 (int)std::lround(touch_x),
                                 (int)std::lround(touch_y), 0, event_time);
                } else {
                    mouse_x = event.motion.x;
                    mouse_y = event.motion.y;
                }
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERUP:
            case SDL_FINGERMOTION: {
                int dw = 0, dh = 0;
                SDL_GetWindowSize(window, &dw, &dh);
                const int action = event.type == SDL_FINGERDOWN
                    ? android_input::kActionDown
                    : event.type == SDL_FINGERUP
                        ? android_input::kActionUp
                        : android_input::kActionMove;
                native_input(env, activity, action,
                             (int)std::lround(event.tfinger.x * dw),
                             (int)std::lround(event.tfinger.y * dh),
                             (jint)(event.tfinger.fingerId & 0x7fffffff),
                             event_time);
                break;
            }

            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                const SDL_Keycode key = event.key.keysym.sym;
                const bool key_down = event.type == SDL_KEYDOWN;
                if (getenv("OPEN_CITADEL_TRACE_INPUT"))
                    fprintf(stderr, "OpenCitadel: key %s %s scancode=%d mod=0x%x\n",
                            SDL_GetKeyName(key), key_down ? "down" : "up",
                            (int)event.key.keysym.scancode,
                            (unsigned)event.key.keysym.mod);
                const bool is_enter = key == SDLK_RETURN ||
                                      key == SDLK_KP_ENTER;
                if (is_enter && !key_down && alt_enter_toggled) {
                    alt_enter_toggled = false;
                    break;
                }
                if (is_enter && alt_enter_toggled)
                    break;
                if (is_enter && key_down &&
                    (event.key.keysym.mod & KMOD_ALT)) {
                    if (!event.key.repeat) {
                        alt_enter_toggled = true;
                        toggle_fullscreen(window);
                    }
                    break;
                }
                if (key == SDLK_F11) {
                    if (key_down && !event.key.repeat)
                        toggle_fullscreen(window);
                    break;
                }
                if (key == SDLK_F1) {
                    if (key_down && !event.key.repeat) {
                        const std::string controls =
                            "W/A/S/D move. Click the ground to walk and "
                            "drag with the mouse to look around. F2 opens "
                            "settings; Escape sends Back to the game. Window "
                            "size and fullscreen apply after restarting.\n\n"
                            "Settings file: " +
                            (settings_path.empty()
                                 ? std::string("unavailable")
                                 : settings_path.string()) +
                            "\nOPEN_CITADEL_* environment options override "
                            "the saved file.";
                        SDL_ShowSimpleMessageBox(
                            SDL_MESSAGEBOX_INFORMATION,
                            "Epic Citadel controls", controls.c_str(), window);
                    }
                    break;
                }
                if (key == SDLK_F2) {
                    if (key_down && !event.key.repeat)
                        show_settings_dialog(window, settings_path, &settings,
                                             &saved_settings);
                    break;
                }
                if (event.key.repeat)
                    break;
                open_citadel::MovementKey movement_key;
                if (open_citadel::movement_key_from_keycode(
                        (int)key, &movement_key)) {
                    if (keyboard_movement.set(movement_key, key_down))
                        send_keyboard_movement(event_time);
                    break;
                }
                const int code = android_keycode(key);
                if (code)
                    native_keyboard(env, activity, 0,
                                    key_down ? android_input::kActionDown
                                             : android_input::kActionUp,
                                    code, unicode_for_key(event.key));
                if (!key_down && key == SDLK_ESCAPE)
                    native_back();
                break;
            }

            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                const int code = android_gamepad_key(
                    (SDL_GameControllerButton)event.cbutton.button);
                if (code)
                    native_joy_button(
                        env, activity, event.cbutton.which,
                        event.type == SDL_CONTROLLERBUTTONDOWN ? 0 : 1,
                        code, event_time);
                break;
            }
            case SDL_CONTROLLERAXISMOTION: {
                const auto axis =
                    (SDL_GameControllerAxis)event.caxis.axis;
                const int aaxis = android_axis(axis);
                if (aaxis >= 0)
                    native_joy_axis(env, activity, event.caxis.which,
                                    2, aaxis,
                                    android_axis_value(axis, event.caxis.value),
                                    event_time);
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (!controller && SDL_IsGameController(event.cdevice.which))
                    controller = SDL_GameControllerOpen(event.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (controller &&
                    SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(controller)) ==
                        event.cdevice.which) {
                    SDL_GameControllerClose(controller);
                    controller = nullptr;
                }
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    int drawable_width = 0;
                    int drawable_height = 0;
                    int window_width = 0;
                    int window_height = 0;
                    SDL_GetWindowSize(window, &window_width, &window_height);
                    SDL_GL_GetDrawableSize(window, &drawable_width,
                                           &drawable_height);
                    if (getenv("OPEN_CITADEL_TRACE_WINDOW")) {
                        fprintf(stderr,
                                "OpenCitadel: resize event=%dx%d window=%dx%d "
                                "drawable=%dx%d; notifying guest\n",
                                event.window.data1, event.window.data2,
                                window_width, window_height,
                                drawable_width, drawable_height);
                    }
                    native_post_init(env, activity,
                                     event.window.data1, event.window.data2);
                } else if ((event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                            event.window.event == SDL_WINDOWEVENT_MINIMIZED) &&
                           !interrupted) {
                    if (mouse_touch_buttons) {
                        native_input(env, activity,
                                     android_input::kActionCancel,
                                     mouse_x, mouse_y, 0, event_time);
                        mouse_touch_buttons = 0;
                    }
                    if (keyboard_movement.clear())
                        send_keyboard_movement(event_time);
                    alt_enter_toggled = false;
                    native_interrupt(env, activity, JNI_TRUE);
                    interrupted = true;
                } else if ((event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                            event.window.event == SDL_WINDOWEVENT_RESTORED) &&
                           interrupted) {
                    native_interrupt(env, activity, JNI_FALSE);
                    interrupted = false;
                }
                break;
            default:
                break;
            }
        }

        const long frames = open_citadel_java_frames_presented();
        if (frames != last_reported && (frames <= 5 || frames % 60 == 0)) {
            fprintf(stderr,
                    "OpenCitadel: frames=%ld draws=%ld textures=%ld atc=%ld "
                    "shaders=%d/%d programs=%d/%d\n",
                    frames, open_citadel_gl_draws(),
                    open_citadel_gl_textures(), open_citadel_gl_atc_decoded(),
                    open_citadel_gl_shaders_ok(),
                    open_citadel_gl_shaders_failed(),
                    open_citadel_gl_programs_ok(),
                    open_citadel_gl_programs_failed());
            last_reported = frames;
        }
        if (frame_limit > 0 && frames >= frame_limit)
            break;
        if (run_seconds > 0 &&
            SDL_GetTicks64() - start >= (Uint64)run_seconds * 1000)
            break;

        SDL_Delay(8);
    }

    if (controller)
        SDL_GameControllerClose(controller);
    if (interrupted)
        native_interrupt(env, activity, JNI_FALSE);

    fprintf(stderr,
            "OpenCitadel: cleanup frames=%ld draws=%ld textures=%ld atc=%ld "
            "shaders=%d/%d programs=%d/%d\n",
            open_citadel_java_frames_presented(), open_citadel_gl_draws(),
            open_citadel_gl_textures(), open_citadel_gl_atc_decoded(),
            open_citadel_gl_shaders_ok(), open_citadel_gl_shaders_failed(),
            open_citadel_gl_programs_ok(), open_citadel_gl_programs_failed());
    native_cleanup(env, activity);

    open_citadel::audio::shutdown();
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
