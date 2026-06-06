#pragma once

#include "eui/dsl.h"
#include "eui/types.h"
#include "eui/window.h"

#include <functional>
#include <string>
#include <vector>

namespace app {

using DslWindowCompose = std::function<void(eui::Ui&, const eui::Screen&)>;

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
const char* trayTitle();
const char* trayIconPath();
bool initialize(eui::window::Handle window);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool externalReady);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool externalReady, bool inputEnabled);
bool isAnimating();
void render(int windowWidth, int windowHeight, float dpiScale);
void releaseGraphicsResources();
void shutdown();
std::vector<DslWindowRequest> consumeWindowRequests();

} // namespace app
