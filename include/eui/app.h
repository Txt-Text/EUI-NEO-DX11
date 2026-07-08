#pragma once

#include "eui/dsl.h"
#include "eui/types.h"
#include "eui/window.h"

#include <functional>
#include <string>
#include <vector>

namespace app {

using DslWindowCompose = std::function<void(eui::Ui&, const eui::Screen&)>;

enum class TitleBarButtonPlacement {
    Left,
    Right
};

// Custom title bar button definition used by registerTitleBarButton().
//
// Requirements:
// - customTitleBar(true) must be enabled on the app config, otherwise the button is never shown.
// - id must be unique within the current title bar registration set.
// - icon should be a glyph string from the configured icon font, for example core::dsl::utf8(0xF013).
// - onClick is required.
//
// Optional state:
// - visible(): return false to hide the button without unregistering it.
// - active(): return true to render the button in its active state.
// - placement: choose whether the button is placed on the left or right side of the title.
struct TitleBarButton {
    std::string id;
    std::string icon;
    std::function<void()> onClick;
    std::function<bool()> visible;
    std::function<bool()> active;
    TitleBarButtonPlacement placement = TitleBarButtonPlacement::Right;
};


struct DslWindowRequest {
    std::string title = "Window";
    std::string pageId = "window";
    eui::Color clearColor = {0.16f, 0.18f, 0.20f, 1.0f};
    int width = 640;
    int height = 420;
    bool modal = false;
    DslWindowCompose compose;
};

const char* windowTitle();
bool showDebugStatsInTitle();
double frameRateLimit();
int initialWindowWidth();
int initialWindowHeight();
bool trayEnabled();
bool customTitleBarEnabled();
const char* trayTitle();
const char* trayIconPath();

// Controls visibility of the built-in system buttons in the custom title bar.
// This only affects the EUI custom title bar path.
void setTitleBarSystemButtons(bool showMinimize, bool showMaximize, bool showClose = true);

// Registers one custom title bar button.
//
// Typical usage:
// - call after app initialization code has set customTitleBar(true)
// - provide a stable id so the same logical button is not registered multiple times
// - call clearTitleBarButtons() before rebuilding the button set
//
// Notes:
// - buttons are rendered in registration order within their placement group
// - right-side custom buttons are laid out before the built-in minimize/maximize/close buttons
// - invalid buttons are ignored: id empty, icon empty, or onClick missing
void registerTitleBarButton(TitleBarButton button);

// Removes all previously registered custom title bar buttons.
// Call this before re-registering a new button set to avoid duplicates.
void clearTitleBarButtons();


void requestUpdate();
bool initialize(eui::window::Handle window);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested, bool inputEnabled);
bool isAnimating();
void render(int windowWidth, int windowHeight, float dpiScale);
void releaseGraphicsResources();
void shutdown();
std::vector<DslWindowRequest> consumeWindowRequests();

namespace detail {
void requestFullPaint();
}

} // namespace app
