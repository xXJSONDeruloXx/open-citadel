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

inline int normalize_movement_keycode(int key)
{
    if (key >= 'A' && key <= 'Z')
        key += 'a' - 'A';
    return key;
}

class MovementKeyBindings {
public:
    bool configure(int forward, int backward, int left, int right)
    {
        forward = normalize_movement_keycode(forward);
        backward = normalize_movement_keycode(backward);
        left = normalize_movement_keycode(left);
        right = normalize_movement_keycode(right);
        if (!forward || !backward || !left || !right ||
            forward == backward || forward == left || forward == right ||
            backward == left || backward == right || left == right)
            return false;
        forward_ = forward;
        backward_ = backward;
        left_ = left;
        right_ = right;
        return true;
    }

    bool set(MovementKey key, int key_code)
    {
        int *binding = binding_for(key);
        key_code = normalize_movement_keycode(key_code);
        if (!binding || !key_code)
            return false;

        if ((key != MovementKey::Forward && forward_ == key_code) ||
            (key != MovementKey::Backward && backward_ == key_code) ||
            (key != MovementKey::Left && left_ == key_code) ||
            (key != MovementKey::Right && right_ == key_code))
            return false;

        *binding = key_code;
        return true;
    }

    int key_code(MovementKey key) const
    {
        switch (key) {
        case MovementKey::Forward: return forward_;
        case MovementKey::Backward: return backward_;
        case MovementKey::Left: return left_;
        case MovementKey::Right: return right_;
        }
        return 0;
    }

    void reset()
    {
        forward_ = 'w';
        backward_ = 's';
        left_ = 'a';
        right_ = 'd';
    }

private:
    int *binding_for(MovementKey key)
    {
        switch (key) {
        case MovementKey::Forward: return &forward_;
        case MovementKey::Backward: return &backward_;
        case MovementKey::Left: return &left_;
        case MovementKey::Right: return &right_;
        }
        return nullptr;
    }

    int forward_ = 'w';
    int backward_ = 's';
    int left_ = 'a';
    int right_ = 'd';
};

inline bool movement_key_from_keycode(int key,
                                      const MovementKeyBindings &bindings,
                                      MovementKey *movement_key)
{
    if (!movement_key)
        return false;
    key = normalize_movement_keycode(key);
    if (key == bindings.key_code(MovementKey::Forward))
        *movement_key = MovementKey::Forward;
    else if (key == bindings.key_code(MovementKey::Backward))
        *movement_key = MovementKey::Backward;
    else if (key == bindings.key_code(MovementKey::Left))
        *movement_key = MovementKey::Left;
    else if (key == bindings.key_code(MovementKey::Right))
        *movement_key = MovementKey::Right;
    else
        return false;
    return true;
}

inline bool movement_key_from_keycode(int key, MovementKey *movement_key)
{
    return movement_key_from_keycode(key, MovementKeyBindings{}, movement_key);
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
