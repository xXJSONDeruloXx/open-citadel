#pragma once

#include <cstdint>
#include <string>

#include <SDL2/SDL.h>

#include "keyboard_controls.h"
#include "settings.h"

namespace open_citadel::desktop_overlay {

enum class CommandType {
    SetMouseSensitivity,
    SetInvertMouseY,
    SetVsync,
    SetUncapFps,
    SetUncappedBenchmark,
    SetFullscreen,
    SaveResolution,
    BeginRebind,
    ResetMovement,
};

struct Command {
    CommandType type = CommandType::SetMouseSensitivity;
    float value = 0.0f;
    bool enabled = false;
    int width = 0;
    int height = 0;
    MovementKey movement_key = MovementKey::Forward;
};

struct Snapshot {
    UserSettings settings;
    bool active_vsync = false;
    bool active_uncap_fps = false;
    bool waiting_for_rebind = false;
    MovementKey rebind_target = MovementKey::Forward;
    std::string rebind_status;
    std::uint64_t revision = 0;
};

void set_open(bool open);
bool is_open();
void push_event(const SDL_Event &event);
bool pop_command(Command *command);
void publish_snapshot(const Snapshot &snapshot);
void request_swap_interval(bool enabled);
void render(SDL_Window *window);
void shutdown();

} // namespace open_citadel::desktop_overlay
