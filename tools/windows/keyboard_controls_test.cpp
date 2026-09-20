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

    open_citadel::MovementKeyBindings bindings;
    CHECK(bindings.configure(1001, 1002, 1003, 1004));
    CHECK(open_citadel::movement_key_from_keycode(1001, bindings, &key));
    CHECK(key == open_citadel::MovementKey::Forward);
    CHECK(open_citadel::movement_key_from_keycode(1004, bindings, &key));
    CHECK(key == open_citadel::MovementKey::Right);
    CHECK(!open_citadel::movement_key_from_keycode('w', bindings, &key));
    CHECK(!bindings.set(open_citadel::MovementKey::Backward, 1001));
    CHECK(!bindings.configure(1001, 1001, 1003, 1004));
    CHECK(bindings.key_code(open_citadel::MovementKey::Forward) == 1001);
    CHECK(bindings.set(open_citadel::MovementKey::Right, 1005));
    CHECK(open_citadel::movement_key_from_keycode(1005, bindings, &key));
    CHECK(key == open_citadel::MovementKey::Right);
    bindings.reset();
    CHECK(bindings.key_code(open_citadel::MovementKey::Forward) == 'w');
    CHECK(bindings.configure('W', 'S', 'A', 'D'));
    CHECK(bindings.key_code(open_citadel::MovementKey::Forward) == 'w');

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
