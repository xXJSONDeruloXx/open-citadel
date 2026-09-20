#ifndef OPEN_CITADEL_KEYBOARD_CONTROLS_H
#define OPEN_CITADEL_KEYBOARD_CONTROLS_H

namespace open_citadel {

enum class MovementKey {
    Forward,
    Backward,
    Left,
    Right,
};

struct MovementAxes {
    float x;
    float y;
};

inline bool movement_key_from_keycode(int key, MovementKey *movement_key)
{
    if (!movement_key)
        return false;
    if (key >= 'A' && key <= 'Z')
        key += 'a' - 'A';
    switch (key) {
    case 'w': *movement_key = MovementKey::Forward; return true;
    case 's': *movement_key = MovementKey::Backward; return true;
    case 'a': *movement_key = MovementKey::Left; return true;
    case 'd': *movement_key = MovementKey::Right; return true;
    default: return false;
    }
}

class KeyboardMovementState {
public:
    bool set(MovementKey key, bool down)
    {
        bool *state = state_for(key);
        if (!state || *state == down)
            return false;
        *state = down;
        return true;
    }

    bool clear()
    {
        const bool was_active = forward_ || backward_ || left_ || right_;
        forward_ = false;
        backward_ = false;
        left_ = false;
        right_ = false;
        return was_active;
    }

    MovementAxes axes() const
    {
        float x = axis(left_, right_);
        float y = axis(forward_, backward_);
        if (x != 0.0f && y != 0.0f) {
            constexpr float kDiagonal = 0.7071067811865475f;
            x *= kDiagonal;
            y *= kDiagonal;
        }
        return {x, y};
    }

private:
    bool *state_for(MovementKey key)
    {
        switch (key) {
        case MovementKey::Forward: return &forward_;
        case MovementKey::Backward: return &backward_;
        case MovementKey::Left: return &left_;
        case MovementKey::Right: return &right_;
        }
        return nullptr;
    }

    static float axis(bool negative, bool positive)
    {
        if (negative == positive)
            return 0.0f;
        return positive ? 1.0f : -1.0f;
    }

    bool forward_ = false;
    bool backward_ = false;
    bool left_ = false;
    bool right_ = false;
};

} // namespace open_citadel

#endif
