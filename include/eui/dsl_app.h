#pragma once

#include "eui/app.h"
#include "eui/async.h"

#include <string>
#include <utility>

namespace app {

struct DslAppConfig {
    const char* titleValue = "App";
    const char* pageIdValue = "app";
    eui::Color clearColorValue = {0.16f, 0.18f, 0.20f, 1.0f};
    int windowWidthValue = 800;
    int windowHeightValue = 600;
#ifndef NDEBUG
    bool showDebugStatsInTitleValue = true;
#else
    bool showDebugStatsInTitleValue = false;
#endif
    double fpsValue = 90.0;
    const char* iconPathValue = "assets/icon.png";
    const char* textFontFileValue = "";
    const char* iconFontFileValue = "";
    bool trayEnabledValue = false;
    const char* trayTitleValue = "";
    const char* trayIconPathValue = "";

    DslAppConfig& title(const char* value) { titleValue = value; return *this; }
    DslAppConfig& pageId(const char* value) { pageIdValue = value; return *this; }
    DslAppConfig& clearColor(const eui::Color& value) { clearColorValue = value; return *this; }
    DslAppConfig& background(const eui::Color& value) { return clearColor(value); }
    DslAppConfig& windowSize(int width, int height) {
        windowWidthValue = width;
        windowHeightValue = height;
        return *this;
    }
    DslAppConfig& windowWidth(int value) { windowWidthValue = value; return *this; }
    DslAppConfig& windowHeight(int value) { windowHeightValue = value; return *this; }
    DslAppConfig& showDebugStatsInTitle(bool value = true) {
        showDebugStatsInTitleValue = value;
        return *this;
    }
    DslAppConfig& fps(double value) { fpsValue = value; return *this; }
    DslAppConfig& iconPath(const char* value) { iconPathValue = value; return *this; }
    DslAppConfig& textFont(const char* value) { textFontFileValue = value; return *this; }
    DslAppConfig& iconFont(const char* value) { iconFontFileValue = value; return *this; }
    DslAppConfig& fonts(const char* textFont, const char* iconFont = "") {
        textFontFileValue = textFont;
        iconFontFileValue = iconFont;
        return *this;
    }
    DslAppConfig& tray(bool value = true) {
        trayEnabledValue = value;
        return *this;
    }
    DslAppConfig& trayTitle(const char* value) {
        trayTitleValue = value;
        return *this;
    }
    DslAppConfig& trayIcon(const char* value) {
        trayIconPathValue = value;
        return *this;
    }
};

struct DslWindowConfig {
    std::string titleValue = "Window";
    std::string pageIdValue = "window";
    eui::Color clearColorValue = {0.16f, 0.18f, 0.20f, 1.0f};
    int windowWidthValue = 640;
    int windowHeightValue = 420;
    bool modalValue = false;

    DslWindowConfig& title(std::string value) { titleValue = std::move(value); return *this; }
    DslWindowConfig& pageId(std::string value) { pageIdValue = std::move(value); return *this; }
    DslWindowConfig& clearColor(const eui::Color& value) { clearColorValue = value; return *this; }
    DslWindowConfig& background(const eui::Color& value) { return clearColor(value); }
    DslWindowConfig& windowSize(int width, int height) {
        windowWidthValue = width;
        windowHeightValue = height;
        return *this;
    }
    DslWindowConfig& windowWidth(int value) { windowWidthValue = value; return *this; }
    DslWindowConfig& windowHeight(int value) { windowHeightValue = value; return *this; }
    DslWindowConfig& modal(bool value = true) { modalValue = value; return *this; }
};

const DslAppConfig& dslAppConfig();
void compose(eui::Ui& ui, const eui::Screen& screen);

void openWindow(const DslWindowConfig& config, DslWindowCompose composeFn);
void openWindow(const char* title, int width, int height, DslWindowCompose composeFn);

} // namespace app
