#pragma once

#include "core/render/render_types.h"

#include <cmath>
#include <string>

namespace core {

enum class CursorShape {
    Arrow,
    Hand
};

struct PointerEvent {
    double x = 0.0;
    double y = 0.0;
    double deltaX = 0.0;
    double deltaY = 0.0;
    bool down = false;
    bool pressedThisFrame = false;
    bool releasedThisFrame = false;
    bool rightDown = false;
    bool rightPressedThisFrame = false;
    bool rightReleasedThisFrame = false;
};

struct KeyboardEvent {
    std::string text;
    std::string pasteText;
    std::string compositionText;
    bool backspace = false;
    bool del = false;
    bool enter = false;
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool home = false;
    bool end = false;
    bool selectAll = false;
    bool copy = false;
    bool cut = false;
    bool undo = false;
    bool redo = false;
    bool shift = false;
    bool escape = false;
    bool composing = false;
    bool compositionChanged = false;

    bool hasInput() const {
        return !text.empty() || !pasteText.empty() || compositionChanged || composing || !compositionText.empty() || backspace || del || enter ||
               left || right || up || down || home || end || selectAll || copy || cut ||
               undo || redo || escape;
    }
};

struct ScrollEvent {
    double x = 0.0;
    double y = 0.0;

    bool active() const {
        return x != 0.0 || y != 0.0;
    }
};

enum class InputKey {
    Backspace,
    Delete,
    Enter,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    Escape,
    A,
    C,
    V,
    X,
    Y,
    Z
};

struct InteractionState {
    bool hover = false;
    bool pressed = false;
    bool clicked = false;
    bool pressStarted = false;
    bool released = false;
    bool drag = false;
    bool active = false;
    bool changed = false;
    double dragStartX = 0.0;
    double dragStartY = 0.0;
    double dragDeltaX = 0.0;
    double dragDeltaY = 0.0;

    void update(const Rect& bounds, const PointerEvent& event, bool topmostHover, bool enabled = true) {
        const bool oldHover = hover;
        const bool oldPressed = pressed;
        const bool oldDrag = drag;
        const bool oldActive = active;

        clicked = false;
        pressStarted = false;
        released = false;

        if (!enabled) {
            hover = false;
            pressed = false;
            drag = false;
            active = false;
            dragDeltaX = 0.0;
            dragDeltaY = 0.0;
            changed = oldHover != hover || oldPressed != pressed || oldDrag != drag || oldActive != active;
            return;
        }

        hover = topmostHover && bounds.contains(event.x, event.y);

        if (hover && event.pressedThisFrame) {
            active = true;
            pressStarted = true;
            dragStartX = event.x;
            dragStartY = event.y;
        }

        pressed = active && event.down;

        dragDeltaX = event.x - dragStartX;
        dragDeltaY = event.y - dragStartY;
        drag = pressed && (std::fabs(dragDeltaX) > 2.0 || std::fabs(dragDeltaY) > 2.0);

        if (event.releasedThisFrame) {
            released = active;
            clicked = active && hover;
            active = false;
            pressed = false;
            drag = false;
        }

        changed = oldHover != hover ||
                  oldPressed != pressed ||
                  oldDrag != drag ||
                  oldActive != active ||
                  pressStarted ||
                  released ||
                  clicked;
    }
};

} // namespace core
