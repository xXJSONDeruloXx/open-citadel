#include <cstdio>

#include "keyboard_controls.h"

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
    open_citadel::MovementKey key;
    CHECK(open_citadel::movement_key_from_keycode('w', &key));
    CHECK(key == open_citadel::MovementKey::Forward);
    CHECK(open_citadel::movement_key_from_keycode('A', &key));
    CHECK(key == open_citadel::MovementKey::Left);
    CHECK(open_citadel::movement_key_from_keycode('s', &key));
    CHECK(key == open_citadel::MovementKey::Backward);
    CHECK(open_citadel::movement_key_from_keycode('d', &key));
    CHECK(key == open_citadel::MovementKey::Right);
    CHECK(!open_citadel::movement_key_from_keycode('e', &key));

    open_citadel::KeyboardMovementState movement;
    auto axes = movement.axes();
    CHECK(axes.x == 0.0f && axes.y == 0.0f);

    CHECK(movement.set(open_citadel::MovementKey::Forward, true));
    axes = movement.axes();
    CHECK(axes.x == 0.0f && axes.y == -1.0f);
    CHECK(!movement.set(open_citadel::MovementKey::Forward, true));

    CHECK(movement.set(open_citadel::MovementKey::Left, true));
    axes = movement.axes();
    CHECK(axes.x == -0.7071067811865475f &&
          axes.y == -0.7071067811865475f);

    CHECK(movement.set(open_citadel::MovementKey::Backward, true));
    axes = movement.axes();
    CHECK(axes.x == -1.0f && axes.y == 0.0f);

    CHECK(movement.set(open_citadel::MovementKey::Forward, false));
    axes = movement.axes();
    CHECK(axes.x == -0.7071067811865475f &&
          axes.y == 0.7071067811865475f);

    CHECK(movement.clear());
    axes = movement.axes();
    CHECK(axes.x == 0.0f && axes.y == 0.0f);
    CHECK(!movement.clear());
    return 0;
}
