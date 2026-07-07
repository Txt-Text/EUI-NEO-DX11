#pragma once

#include "core/input/input_types.h"
#include "core/window/window_backend.h"

#include <unordered_map>
#include <utility>

namespace core {

namespace detail {

struct InputQueue {
    std::string text;
    std::string pasteText;
    std::string compositionText;
    double scrollX = 0.0;
    double scrollY = 0.0;
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
    bool compositionChanged = false;
};

struct PointerState {
    double lastX = 0.0;
    double lastY = 0.0;
    bool lastDown = false;
    bool lastRightDown = false;
};

inline std::unordered_map<window::Handle, InputQueue>& inputQueues() {
    static std::unordered_map<window::Handle, InputQueue> queues;
    return queues;
}

inline std::unordered_map<window::Handle, PointerState>& pointerStates() {
    static std::unordered_map<window::Handle, PointerState> states;
    return states;
}

inline std::unordered_map<window::Handle, bool>& composingStates() {
    static std::unordered_map<window::Handle, bool> states;
    return states;
}

inline std::unordered_map<window::Handle, std::string>& compositionTextStates() {
    static std::unordered_map<window::Handle, std::string> states;
    return states;
}

inline InputQueue& inputQueue(window::Handle window) {
    return inputQueues()[window];
}

inline PointerState& pointerState(window::Handle window) {
    return pointerStates()[window];
}

inline bool isComposing(window::Handle window) {
    const auto it = composingStates().find(window);
    return it != composingStates().end() && it->second;
}

inline std::string compositionText(window::Handle window) {
    const auto it = compositionTextStates().find(window);
    return it == compositionTextStates().end() ? std::string{} : it->second;
}

inline void setComposing(window::Handle window, bool composing) {
    composingStates()[window] = composing;
}

inline void setCompositionText(window::Handle window, const std::string& text) {
    compositionTextStates()[window] = text;
}

inline void appendUtf8(std::string& output, unsigned int codepoint) {
    if (codepoint < 0x20) {
        return;
    }
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

} // namespace detail

inline void queueTextInput(window::Handle window, const std::string& text) {
    detail::InputQueue& queue = detail::inputQueue(window);
    queue.text += text;
    queue.compositionText.clear();
    queue.compositionChanged = true;
    detail::setComposing(window, false);
    detail::setCompositionText(window, {});
}

inline void queueTextEditing(window::Handle window, const std::string& text) {
    detail::InputQueue& queue = detail::inputQueue(window);
    queue.compositionText = text;
    queue.compositionChanged = true;
    detail::setComposing(window, !text.empty());
    detail::setCompositionText(window, text);
}

inline void queueScrollInput(window::Handle window, double x, double y) {
    detail::InputQueue& queue = detail::inputQueue(window);
    queue.scrollX += x;
    queue.scrollY += y;
}

inline void queueKeyInput(window::Handle window, InputKey key, bool ctrl = false, bool shift = false) {
    detail::InputQueue& queue = detail::inputQueue(window);
    queue.shift = shift;
    if (ctrl && key == InputKey::V) {
        queue.pasteText += core::window::clipboardText(window);
        return;
    }
    if (ctrl && key == InputKey::C) {
        queue.copy = true;
        return;
    }
    if (ctrl && key == InputKey::X) {
        queue.cut = true;
        return;
    }
    if (ctrl && key == InputKey::Z) {
        shift ? queue.redo = true : queue.undo = true;
        return;
    }
    if (ctrl && key == InputKey::Y) {
        queue.redo = true;
        return;
    }
    if (ctrl && key == InputKey::A) {
        queue.selectAll = true;
        return;
    }

    if (detail::isComposing(window) && (key == InputKey::Backspace || key == InputKey::Delete)) {
        return;
    }

    switch (key) {
    case InputKey::Backspace: queue.backspace = true; break;
    case InputKey::Delete: queue.del = true; break;
    case InputKey::Enter: queue.enter = true; break;
    case InputKey::Left: queue.left = true; break;
    case InputKey::Right: queue.right = true; break;
    case InputKey::Up: queue.up = true; break;
    case InputKey::Down: queue.down = true; break;
    case InputKey::Home: queue.home = true; break;
    case InputKey::End: queue.end = true; break;
    case InputKey::Escape: queue.escape = true; break;
    default: break;
    }
}

inline void installInputCallbacks(window::Handle window) {
    (void)window;
}

inline std::pair<KeyboardEvent, ScrollEvent> consumeInputEvents(window::Handle window) {
    detail::InputQueue& queue = detail::inputQueue(window);
    const bool wasComposing = detail::isComposing(window);
    const std::string previousCompositionText = detail::compositionText(window);
    const bool queuedCompositionChanged = queue.compositionChanged;
    KeyboardEvent keyboard;
    keyboard.text = std::move(queue.text);
    keyboard.pasteText = std::move(queue.pasteText);
    keyboard.compositionText = std::move(queue.compositionText);
    keyboard.backspace = queue.backspace;
    keyboard.del = queue.del;
    keyboard.enter = queue.enter;
    keyboard.left = queue.left;
    keyboard.right = queue.right;
    keyboard.up = queue.up;
    keyboard.down = queue.down;
    keyboard.home = queue.home;
    keyboard.end = queue.end;
    keyboard.selectAll = queue.selectAll;
    keyboard.copy = queue.copy;
    keyboard.cut = queue.cut;
    keyboard.undo = queue.undo;
    keyboard.redo = queue.redo;
    keyboard.shift = queue.shift;
    keyboard.escape = queue.escape;
    keyboard.composing = detail::isComposing(window);
    if (!queuedCompositionChanged && keyboard.composing) {
        keyboard.compositionText = previousCompositionText;
    }
    if (!queuedCompositionChanged && !keyboard.composing) {
        keyboard.compositionText.clear();
    }
    keyboard.compositionChanged = queuedCompositionChanged ||
                                  wasComposing != keyboard.composing ||
                                  previousCompositionText != keyboard.compositionText;
    detail::setCompositionText(window, keyboard.compositionText);

    ScrollEvent scroll{queue.scrollX, queue.scrollY};
    queue = {};
    return {std::move(keyboard), scroll};
}

inline bool hasPendingPointerInput(window::Handle window, float dpiScale = 1.0f) {
    const auto stateIt = detail::pointerStates().find(window);
    if (stateIt == detail::pointerStates().end()) {
        return false;
    }

    double x = 0.0;
    double y = 0.0;
    core::window::getCursorPosition(window, x, y);
    x *= dpiScale;
    y *= dpiScale;

    const detail::PointerState& state = stateIt->second;
    return x != state.lastX ||
           y != state.lastY ||
           core::window::isMouseButtonDown(window, 0) != state.lastDown ||
           core::window::isMouseButtonDown(window, 1) != state.lastRightDown;
}

inline void releaseInputQueue(window::Handle window) {
    detail::inputQueues().erase(window);
    detail::pointerStates().erase(window);
    detail::composingStates().erase(window);
    detail::compositionTextStates().erase(window);
}

inline PointerEvent readPointerEvent(window::Handle window, float dpiScale = 1.0f) {
    detail::PointerState& state = detail::pointerState(window);

    double x = 0.0;
    double y = 0.0;
    core::window::getCursorPosition(window, x, y);
    x *= dpiScale;
    y *= dpiScale;

    PointerEvent event;
    event.x = x;
    event.y = y;
    event.deltaX = x - state.lastX;
    event.deltaY = y - state.lastY;
    event.down = core::window::isMouseButtonDown(window, 0);
    event.rightDown = core::window::isMouseButtonDown(window, 1);
    event.pressedThisFrame = event.down && !state.lastDown;
    event.releasedThisFrame = !event.down && state.lastDown;
    event.rightPressedThisFrame = event.rightDown && !state.lastRightDown;
    event.rightReleasedThisFrame = !event.rightDown && state.lastRightDown;

    state.lastX = x;
    state.lastY = y;
    state.lastDown = event.down;
    state.lastRightDown = event.rightDown;
    return event;
}

} // namespace core
