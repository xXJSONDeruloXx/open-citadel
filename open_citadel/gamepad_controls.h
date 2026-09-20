#pragma once

#include <algorithm>

#include <SDL2/SDL.h>

#include "android_input_codes.h"

namespace open_citadel {

inline int gamepad_button_keycode(SDL_GameControllerButton button) noexcept
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

inline int gamepad_axis_id(SDL_GameControllerAxis axis) noexcept
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

inline float normalize_gamepad_axis(SDL_GameControllerAxis axis,
                                    Sint16 raw) noexcept
{
    if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
        axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
    return std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
}

} // namespace open_citadel
