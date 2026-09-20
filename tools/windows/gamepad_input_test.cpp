#define SDL_MAIN_HANDLED

#include <cmath>
#include <cstdio>
#include <string>

#include <SDL2/SDL.h>

#include "android_input_codes.h"
#include "gamepad_controls.h"

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
    CHECK(open_citadel::gamepad_button_keycode(SDL_CONTROLLER_BUTTON_A) ==
          android_input::kKeyButtonA);
    CHECK(open_citadel::gamepad_button_keycode(
              SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ==
          android_input::kKeyButtonR1);
    CHECK(open_citadel::gamepad_button_keycode(SDL_CONTROLLER_BUTTON_DPAD_UP) ==
          android_input::kKeyDpadUp);
    CHECK(open_citadel::gamepad_button_keycode(SDL_CONTROLLER_BUTTON_MAX) == 0);
    CHECK(open_citadel::gamepad_axis_id(SDL_CONTROLLER_AXIS_LEFTX) ==
          android_input::kAxisX);
    CHECK(open_citadel::gamepad_axis_id(SDL_CONTROLLER_AXIS_RIGHTY) ==
          android_input::kAxisRz);
    CHECK(open_citadel::gamepad_axis_id(SDL_CONTROLLER_AXIS_TRIGGERLEFT) ==
          android_input::kAxisLtrigger);
    CHECK(open_citadel::gamepad_axis_id(SDL_CONTROLLER_AXIS_MAX) == -1);
    CHECK(std::fabs(open_citadel::normalize_gamepad_axis(
                        SDL_CONTROLLER_AXIS_LEFTX, 16384) - 0.500015f) <
          0.001f);
    CHECK(open_citadel::normalize_gamepad_axis(
              SDL_CONTROLLER_AXIS_LEFTY, -32768) == -1.0f);
    CHECK(std::fabs(open_citadel::normalize_gamepad_axis(
                        SDL_CONTROLLER_AXIS_TRIGGERLEFT, 16384) - 0.500015f) <
          0.001f);

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    const int device_index = SDL_JoystickAttachVirtual(
        SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 11, 1);
    CHECK(device_index >= 0);

    char guid[33] = {};
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device_index),
                              guid, sizeof(guid));
    const std::string mapping = std::string(guid) +
        ",Open Citadel virtual pad,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,"
        "start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,"
        "rightshoulder:b10,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,"
        "dpright:h0.2,leftx:a0,lefty:a1,rightx:a2,righty:a3,"
        "lefttrigger:a4,righttrigger:a5";
    CHECK(SDL_GameControllerAddMapping(mapping.c_str()) >= 0);
    CHECK(SDL_IsGameController(device_index) == SDL_TRUE);

    SDL_GameController *controller = SDL_GameControllerOpen(device_index);
    CHECK(controller != nullptr);
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);
    CHECK(joystick != nullptr);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

    CHECK(SDL_JoystickSetVirtualAxis(joystick, 0, 16384) == 0);
    CHECK(SDL_JoystickSetVirtualAxis(joystick, 1, -16384) == 0);
    CHECK(SDL_JoystickSetVirtualButton(joystick, 0, SDL_PRESSED) == 0);
    SDL_JoystickUpdate();

    bool left_x_seen = false;
    bool left_y_seen = false;
    bool button_a_seen = false;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERAXISMOTION) {
            const auto axis =
                static_cast<SDL_GameControllerAxis>(event.caxis.axis);
            const int guest_axis = open_citadel::gamepad_axis_id(axis);
            if (guest_axis == android_input::kAxisX) {
                left_x_seen = std::fabs(
                    open_citadel::normalize_gamepad_axis(axis,
                                                         event.caxis.value) -
                    0.500015f) < 0.001f;
            } else if (guest_axis == android_input::kAxisY) {
                left_y_seen = std::fabs(
                    open_citadel::normalize_gamepad_axis(axis,
                                                         event.caxis.value) +
                    0.500015f) < 0.001f;
            }
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
            button_a_seen = open_citadel::gamepad_button_keycode(
                                static_cast<SDL_GameControllerButton>(
                                    event.cbutton.button)) ==
                            android_input::kKeyButtonA;
        }
    }

    CHECK(left_x_seen);
    CHECK(left_y_seen);
    CHECK(button_a_seen);

    SDL_GameControllerClose(controller);
    CHECK(SDL_JoystickDetachVirtual(device_index) == 0);
    SDL_Quit();
    return 0;
}
