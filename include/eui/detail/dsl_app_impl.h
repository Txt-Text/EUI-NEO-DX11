#pragma once

#include "eui/dsl_app.h"
#include "eui/network.h"
#include "eui/platform.h"

#include "3rd/stb_image.h"
#include "components/button.h"
#include "components/mousearea.h"
#include "components/theme.h"
#include "core/dsl_runtime.h"
#include "core/platform/platform.h"
#include "core/render/text.h"
#include "core/window/window_backend.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>







#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace app {

namespace detail {

inline core::dsl::Runtime& dslRuntime() {
    static core::dsl::Runtime runtime;
    return runtime;
}

inline std::vector<DslWindowRequest>& dslWindowRequests() {
    static std::vector<DslWindowRequest> requests;
    return requests;
}

struct TitleBarState {
    bool showMinimize = true;
    bool showMaximize = true;
    bool showClose = true;
    std::vector<TitleBarButton> buttons;
};

inline TitleBarState& titleBarState() {
    static TitleBarState state;
    return state;
}

struct DslAppState {
    bool composed = false;
    bool iconApplied = false;
    float logicalWidth = 0.0f;
    float logicalHeight = 0.0f;
    core::window::Handle window = nullptr;
};

inline DslAppState& dslAppState() {
    static DslAppState state;
    return state;
}





inline constexpr float kCustomTitleBarHeight = 38.0f;
inline constexpr float kCustomTitleBarResizeBorder = 8.0f;
inline constexpr float kCustomTitleBarTitleFontSize = 14.0f;
inline constexpr float kCustomTitleBarTitleLineHeight = 20.0f;
inline constexpr float kCustomTitleBarButtonSize = 30.0f;
inline constexpr float kCustomTitleBarButtonInset = 4.0f;
inline constexpr float kCustomTitleBarButtonGap = 4.0f;
inline constexpr float kCustomTitleBarIconSize = 18.0f;
inline constexpr float kCustomTitleBarIconInset = 10.0f;

inline constexpr float kCustomTitleBarTitleGap = 8.0f;
inline constexpr float kCustomTitleBarDragMargin = 6.0f;

inline bool customTitleBarEnabled() {
#if defined(_WIN32)
    return dslAppConfig().customTitleBarValue;
#else
    return false;
#endif
}


inline float customTitleBarHeight() {
    return customTitleBarEnabled() ? kCustomTitleBarHeight : 0.0f;
}

inline std::string resolveIconPath(const char* iconPath) {
    if (iconPath == nullptr || iconPath[0] == '\0') {
        return {};
    }

    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path requested(iconPath);
    const fs::path current = fs::current_path(error);
    std::vector<fs::path> candidates;
    candidates.push_back(requested);
    if (!error) {
        candidates.push_back(current / requested);
        candidates.push_back(current / "assets" / requested.filename());
    }

    fs::path executableDir;
#if defined(__APPLE__)
    char executablePath[4096];
    uint32_t executablePathSize = sizeof(executablePath);
    if (_NSGetExecutablePath(executablePath, &executablePathSize) == 0) {
        executableDir = fs::absolute(fs::path(executablePath), error).parent_path();
    }
#elif defined(_WIN32)
    char executablePath[MAX_PATH];
    const DWORD executablePathSize = GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    if (executablePathSize > 0 && executablePathSize < MAX_PATH) {
        executableDir = fs::absolute(fs::path(executablePath), error).parent_path();
    }
#elif defined(__linux__)
    char executablePath[4096];
    const ssize_t executablePathSize = readlink("/proc/self/exe", executablePath, sizeof(executablePath) - 1);
    if (executablePathSize > 0) {
        executablePath[executablePathSize] = '\0';
        executableDir = fs::absolute(fs::path(executablePath), error).parent_path();
    }
#endif
    if (!executableDir.empty()) {
        candidates.push_back(executableDir / requested);
        candidates.push_back(executableDir / "assets" / requested.filename());
    }

    for (const fs::path& candidate : candidates) {
        error.clear();
        if (fs::exists(candidate, error) && !error) {
            return fs::absolute(candidate, error).string();
        }
    }
    return {};
}

inline void applyWindowIcon(core::window::Handle window) {
    if (window == nullptr) {
        return;
    }

    const std::string iconPath = resolveIconPath(dslAppConfig().iconPathValue);
    if (iconPath.empty()) {
        return;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* pixels = stbi_load(iconPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return;
    }

    core::window::setWindowIcon(window, width, height, pixels);
    stbi_image_free(pixels);
}



inline std::string titleBarIconSource() {
    return resolveIconPath("assets/icon_titlebar.png");
}









inline components::theme::ThemeColorTokens customTitleBarTokens() {
    const eui::Color clear = dslAppConfig().clearColorValue;
    const float luminance = clear.r * 0.2126f + clear.g * 0.7152f + clear.b * 0.0722f;
    const bool dark = luminance < 0.48f;
    components::theme::ThemeColorTokens tokens = dark
        ? components::theme::dark()
        : components::theme::light();

    tokens.surface = clear;
    tokens.surfaceHover = core::mixColor(
        clear,
        dark ? eui::Color{1.0f, 1.0f, 1.0f, 1.0f} : eui::Color{0.0f, 0.0f, 0.0f, 1.0f},
        dark ? 0.08f : 0.05f);
    tokens.surfaceActive = core::mixColor(
        clear,
        dark ? eui::Color{1.0f, 1.0f, 1.0f, 1.0f} : eui::Color{0.0f, 0.0f, 0.0f, 1.0f},
        dark ? 0.14f : 0.10f);
    return tokens;
}


inline void showWindowMenuAtClientPoint(core::window::Handle window, float clientX, float clientY) {
#if defined(_WIN32)
    HWND hwnd = static_cast<HWND>(window);
    if (hwnd == nullptr) {
        return;
    }

    POINT point{};
    point.x = static_cast<LONG>(std::lround(clientX));
    point.y = static_cast<LONG>(std::lround(clientY));
    if (ClientToScreen(hwnd, &point)) {
        core::platform::showWindowSystemMenu(window, point.x, point.y);
    }
#else
    (void)window;
    (void)clientX;
    (void)clientY;
#endif
}

inline components::ButtonStyle titleBarButtonStyle(const components::theme::ThemeColorTokens& tokens,
                                                   bool closeButton,
                                                   bool active = false) {
    components::ButtonStyle style(tokens, false);
    style.radius = 7.0f;
    style.normal = active
        ? components::theme::withAlpha(tokens.surfaceActive, 0.92f)
        : eui::Color{0.0f, 0.0f, 0.0f, 0.0f};
    style.hover = closeButton
        ? eui::Color{0.82f, 0.24f, 0.20f, 0.96f}
        : components::theme::withAlpha(tokens.surfaceHover, 0.96f);
    style.pressed = closeButton
        ? eui::Color{0.70f, 0.18f, 0.16f, 0.98f}
        : components::theme::withAlpha(tokens.surfaceActive, 0.98f);
    style.text = components::theme::withAlpha(tokens.text, 0.95f);
    style.icon = style.text;
    style.border = {0.0f, {0.0f, 0.0f, 0.0f, 0.0f}};
    style.shadow = {false, {0.0f, 0.0f}, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f, 0.0f}};
    style.pressScale = 1.0f;
    return style;
}

inline int visibleCustomButtonCount(TitleBarButtonPlacement placement) {
    int count = 0;
    for (const TitleBarButton& button : titleBarState().buttons) {
        const bool visible = !button.visible || button.visible();
        if (visible && button.placement == placement) {
            ++count;
        }
    }
    return count;
}

inline float systemButtonSlotWidth() {
    const TitleBarState& state = titleBarState();
    int count = 0;
    if (state.showMinimize) {
        ++count;
    }
    if (state.showMaximize) {
        ++count;
    }
    if (state.showClose) {
        ++count;
    }
    if (count <= 0) {
        return 0.0f;
    }
        return count * kCustomTitleBarButtonSize;
}


inline void composeTitleBarButton(core::dsl::Ui& ui,
                                  const std::string& id,
                                  float x,
                                  const std::string& icon,
                                  const components::theme::ThemeColorTokens& tokens,
                                  std::function<void()> onClick,
                                  bool closeButton = false,
                                  bool active = false,
                                  bool systemButton = false) {
    components::ButtonStyle style = titleBarButtonStyle(tokens, closeButton, active);
    if (systemButton) {
        style.radius = 0.0f;
    }
    const float buttonY = systemButton ? 0.0f : kCustomTitleBarButtonInset;
    const float buttonHeight = systemButton ? kCustomTitleBarHeight : kCustomTitleBarButtonSize;

    ui.stack(id + ".wrap")
        .x(x)
        .y(buttonY)
        .size(kCustomTitleBarButtonSize, buttonHeight)
        .content([&] {
            components::button(ui, id)
                .size(kCustomTitleBarButtonSize, buttonHeight)
                .text("")
                .icon(icon)
                .fontSize(12.0f)
                .iconSize(12.0f)
                .style(style)
                .transition(0.12f)
                .onClick(std::move(onClick))
                .build();
        })
        .build();
}


inline void composeCustomTitleBar(core::dsl::Ui& ui, float width) {
    const components::theme::ThemeColorTokens tokens = customTitleBarTokens();
    const TitleBarState& barState = titleBarState();
    const float barHeight = kCustomTitleBarHeight;
    const float resizeBorder = kCustomTitleBarResizeBorder;
    const float iconX = kCustomTitleBarIconInset;
    const float iconY = std::floor((barHeight - kCustomTitleBarIconSize) * 0.5f);
    const float iconRight = iconX + kCustomTitleBarIconSize;
    const int leftButtonCount = visibleCustomButtonCount(TitleBarButtonPlacement::Left);
    const int rightButtonCount = visibleCustomButtonCount(TitleBarButtonPlacement::Right);
    const float leftButtonsWidth = leftButtonCount > 0
        ? leftButtonCount * kCustomTitleBarButtonSize + (leftButtonCount - 1) * kCustomTitleBarButtonGap
        : 0.0f;
    const float rightCustomWidth = rightButtonCount > 0
        ? rightButtonCount * kCustomTitleBarButtonSize + (rightButtonCount - 1) * kCustomTitleBarButtonGap
        : 0.0f;
    const float rightSystemWidth = systemButtonSlotWidth();
    const float rightControlsWidth = rightCustomWidth + rightSystemWidth + ((rightCustomWidth > 0.0f && rightSystemWidth > 0.0f) ? kCustomTitleBarButtonGap : 0.0f);
    const float titleLeft = iconRight + kCustomTitleBarTitleGap + (leftButtonsWidth > 0.0f ? (kCustomTitleBarTitleGap + leftButtonsWidth) : 0.0f);
    const float titleRightPadding = kCustomTitleBarIconInset + rightControlsWidth;
        const float titleTop = 0.0f;
    const float titleWidth = std::max(0.0f, width - titleLeft - titleRightPadding);



    const float dragX = std::max(resizeBorder, titleLeft - kCustomTitleBarDragMargin);
    const float dragRight = std::max(dragX, width - std::max(resizeBorder, titleRightPadding + kCustomTitleBarDragMargin));
    const float dragY = resizeBorder;
    const float dragWidth = std::max(0.0f, dragRight - dragX);
    const float dragHeight = std::max(0.0f, barHeight - dragY);
        const float rightOrigin = width - rightControlsWidth;


    ui.rect("eui.custom_title_bar.bg")
        .size(width, barHeight)
        .color(tokens.surface)
        .build();

    ui.rect("eui.custom_title_bar.border")
        .y(barHeight - 1.0f)
        .size(width, 1.0f)
        .color(components::theme::withAlpha(tokens.border, tokens.dark ? 0.85f : 0.65f))
        .build();

            const std::string iconSource = titleBarIconSource();
    if (!iconSource.empty()) {
        ui.image("eui.custom_title_bar.icon")
            .x(iconX)
            .y(iconY)
            .size(kCustomTitleBarIconSize, kCustomTitleBarIconSize)
            .source(iconSource)
            .contain()
            .build();
    }



    components::mouseArea(ui, "eui.custom_title_bar.icon_hit")
        .x(std::max(0.0f, iconX - 4.0f))
        .y(0.0f)
        .size(kCustomTitleBarIconSize + 8.0f, barHeight)
        .cursor(core::CursorShape::Arrow)
        .onTap([](const components::MouseEvent& event) {
            core::window::Handle window = dslAppState().window;
            if (window != nullptr) {
                showWindowMenuAtClientPoint(window, event.bounds.x, event.bounds.y + event.bounds.height);
            }
        })
        .onContextMenu([](const components::MouseEvent& event) {
            core::window::Handle window = dslAppState().window;
            if (window != nullptr) {
                showWindowMenuAtClientPoint(window, event.bounds.x, event.bounds.y + event.bounds.height);
            }
        })
        .build();

    float leftButtonX = iconRight + kCustomTitleBarTitleGap;
    for (const TitleBarButton& button : barState.buttons) {
        const bool visible = !button.visible || button.visible();
        if (!visible || button.placement != TitleBarButtonPlacement::Left) {
            continue;
        }
        composeTitleBarButton(ui,
                              "eui.custom_title_bar.left_button." + button.id,
                              leftButtonX,
                              button.icon,
                              tokens,
                              button.onClick,
                              false,
                              button.active && button.active());
        leftButtonX += kCustomTitleBarButtonSize + kCustomTitleBarButtonGap;
    }

                ui.text("eui.custom_title_bar.title")
        .x(titleLeft)
        .y(titleTop)
        .size(titleWidth, barHeight)
        .text(app::windowTitle())
        .fontSize(kCustomTitleBarTitleFontSize)
        .lineHeight(barHeight)
        .color(components::theme::withAlpha(tokens.text, 0.96f))
        .verticalAlign(core::VerticalAlign::Center)
        .build();






    components::mouseArea(ui, "eui.custom_title_bar.drag")
        .x(dragX)
        .y(dragY)
        .size(dragWidth, dragHeight)
        .cursor(core::CursorShape::Arrow)
        .onPress([](const components::MouseEvent&) {
            core::window::Handle window = dslAppState().window;
            if (window != nullptr) {
                eui::platform::beginWindowDrag(window);
            }
        })
        .onContextMenu([](const components::MouseEvent& event) {
            core::window::Handle window = dslAppState().window;
            if (window != nullptr) {
                showWindowMenuAtClientPoint(window, event.x, event.y);
            }
        })
        .build();

    float rightX = rightOrigin;
    for (const TitleBarButton& button : barState.buttons) {
        const bool visible = !button.visible || button.visible();
        if (!visible || button.placement != TitleBarButtonPlacement::Right) {
            continue;
        }
        composeTitleBarButton(ui,
                              "eui.custom_title_bar.right_button." + button.id,
                              rightX,
                              button.icon,
                              tokens,
                              button.onClick,
                              false,
                              button.active && button.active());
        rightX += kCustomTitleBarButtonSize + kCustomTitleBarButtonGap;
    }
    if (rightCustomWidth > 0.0f && rightSystemWidth > 0.0f) {
        rightX += kCustomTitleBarButtonGap;
    }

    core::window::Handle window = dslAppState().window;
        if (barState.showMinimize) {
        composeTitleBarButton(ui,
                              "eui.custom_title_bar.minimize",
                              rightX,
                              core::dsl::utf8(0xF068),
                              tokens,
                              [window] {
                                  if (window != nullptr) {
                                      eui::platform::minimizeWindow(window);
                                  }
                              },
                              false,
                              false,
                              true);
        rightX += kCustomTitleBarButtonSize;
    }
    if (barState.showMaximize) {
        const bool maximized = window != nullptr && eui::platform::isWindowMaximized(window);
        composeTitleBarButton(ui,
                              "eui.custom_title_bar.maximize",
                              rightX,
                              maximized ? core::dsl::utf8(0xF2D2) : core::dsl::utf8(0xF2D0),
                              tokens,
                              [window, maximized] {
                                  if (window == nullptr) {
                                      return;
                                  }
                                  if (maximized) {
                                      eui::platform::restoreWindow(window);
                                  } else {
                                      eui::platform::maximizeWindow(window);
                                  }
                              },
                              false,
                              false,
                              true);
        rightX += kCustomTitleBarButtonSize;
    }
    if (barState.showClose) {
        composeTitleBarButton(ui,
                              "eui.custom_title_bar.close",
                              rightX,
                              core::dsl::utf8(0xF00D),
                              tokens,
                              [window] {
                                  if (window != nullptr) {
                                      eui::platform::closeWindow(window);
                                  }
                              },
                              true,
                              false,
                              true);
    }

}

inline void requestFullPaint() {
    dslRuntime().requestFullPaint();
    core::platform::requestUiUpdate();
}

inline void cancelPointerInteractions() {
    dslRuntime().cancelPointerInteractions();
}

} // namespace detail


inline void openWindow(const DslWindowConfig& config, DslWindowCompose composeFn) {


    if (!composeFn) {
        return;
    }

    DslWindowRequest request;
    request.title = config.titleValue.empty() ? "Window" : config.titleValue;
    request.pageId = config.pageIdValue.empty() ? request.title : config.pageIdValue;
    request.clearColor = config.clearColorValue;
    request.width = std::max(160, config.windowWidthValue);
    request.height = std::max(120, config.windowHeightValue);
    request.modal = config.modalValue;
    request.compose = std::move(composeFn);
    detail::dslWindowRequests().push_back(std::move(request));
    requestUpdate();
}

inline void openWindow(const char* title, int width, int height, DslWindowCompose composeFn) {


    openWindow(DslWindowConfig{}
                   .title(title != nullptr ? title : "Window")
                   .pageId(title != nullptr ? title : "window")
                   .windowSize(width, height),
               std::move(composeFn));
}

inline std::vector<DslWindowRequest> consumeWindowRequests() {


    std::vector<DslWindowRequest> requests = std::move(detail::dslWindowRequests());
    detail::dslWindowRequests().clear();
    return requests;
}

inline const char* windowTitle() {


    return dslAppConfig().titleValue;
}

inline bool showDebugStatsInTitle() {


    return dslAppConfig().showDebugStatsInTitleValue;
}

inline double frameRateLimit() {


    return dslAppConfig().fpsValue;
}

inline int initialWindowWidth() {


    return dslAppConfig().windowWidthValue;
}

inline int initialWindowHeight() {


    return dslAppConfig().windowHeightValue;
}

inline bool trayEnabled() {


    return dslAppConfig().trayEnabledValue;
}

inline bool customTitleBarEnabled() {
    return detail::customTitleBarEnabled();
}



inline const char* trayTitle() {


    const DslAppConfig& config = dslAppConfig();
    return (config.trayTitleValue != nullptr && config.trayTitleValue[0] != '\0')
        ? config.trayTitleValue
        : config.titleValue;
}

inline const char* trayIconPath() {


    const DslAppConfig& config = dslAppConfig();
    return (config.trayIconPathValue != nullptr && config.trayIconPathValue[0] != '\0')
        ? config.trayIconPathValue
        : config.iconPathValue;
}

inline void setTitleBarSystemButtons(bool showMinimize, bool showMaximize, bool showClose) {


    detail::TitleBarState& state = detail::titleBarState();
    state.showMinimize = showMinimize;
    state.showMaximize = showMaximize;
    state.showClose = showClose;
    requestUpdate();
}

inline void registerTitleBarButton(TitleBarButton button) {


    if (button.id.empty() || button.icon.empty() || !button.onClick) {
        return;
    }
    detail::titleBarState().buttons.push_back(std::move(button));
    requestUpdate();
}

inline void clearTitleBarButtons() {


    detail::titleBarState().buttons.clear();
    requestUpdate();
}

inline void requestUpdate() {


    core::platform::requestUiUpdate();
}

inline bool initialize(core::window::Handle window) {


    const DslAppConfig& config = dslAppConfig();
    core::TextPrimitive::setDefaultFontFiles(
        config.textFontFileValue != nullptr ? config.textFontFileValue : "",
        config.iconFontFileValue != nullptr ? config.iconFontFileValue : "");

    detail::DslAppState& state = detail::dslAppState();
    state.window = window;
    core::window::setWindowCustomTitleBar(window, detail::customTitleBarEnabled());
    if (!state.iconApplied) {
        detail::applyWindowIcon(window);
        state.iconApplied = true;
    }
    return detail::dslRuntime().initialize(window);
}

inline bool update(core::window::Handle window,
                   float deltaSeconds,
                   int windowWidth,
                   int windowHeight,
                   float dpiScale,
                   float pointerScale) {


    const bool asyncReady = core::async::dispatchReady();
    const bool updateRequested = core::platform::consumeUiUpdate();
    return update(window, deltaSeconds, windowWidth, windowHeight, dpiScale, pointerScale, updateRequested || asyncReady);
}

inline bool update(core::window::Handle window,
                   float deltaSeconds,
                   int windowWidth,
                   int windowHeight,
                   float dpiScale,
                   float pointerScale,
                   bool updateRequested) {


    return update(window, deltaSeconds, windowWidth, windowHeight, dpiScale, pointerScale, updateRequested, true);
}

inline bool update(core::window::Handle window,
                   float deltaSeconds,
                   int windowWidth,
                   int windowHeight,
                   float dpiScale,
                   float pointerScale,
                   bool updateRequested,
                   bool inputEnabled) {


    if (windowWidth <= 0 || windowHeight <= 0 || dpiScale <= 0.0f) {
        return false;
    }

    const DslAppConfig& config = dslAppConfig();
    const float titleBarHeight = detail::customTitleBarHeight();
    const float logicalWidth = static_cast<float>(windowWidth) / dpiScale;
    const float logicalHeight = static_cast<float>(windowHeight) / dpiScale;
    const float contentLogicalHeight = std::max(0.0f, logicalHeight - titleBarHeight);
    detail::DslAppState& state = detail::dslAppState();
    state.window = window;

    const auto composeFrame = [&] {
        detail::dslRuntime().compose(config.pageIdValue, logicalWidth, logicalHeight, [&](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
            if (detail::customTitleBarEnabled()) {
                ui.stack("eui.custom_title_bar.root")
                    .size(screen.width, screen.height)
                    .content([&] {
                        detail::composeCustomTitleBar(ui, screen.width);

                        ui.stack("eui.custom_title_bar.content")
                            .y(titleBarHeight)
                            .size(screen.width, contentLogicalHeight)
                            .content([&] {
                                core::dsl::Screen contentScreen;
                                contentScreen.width = screen.width;
                                contentScreen.height = contentLogicalHeight;
                                compose(ui, contentScreen);
                            })
                            .build();
                    })
                    .build();
            } else {
                compose(ui, screen);
            }
        });
        state.composed = true;
        state.logicalWidth = logicalWidth;
        state.logicalHeight = logicalHeight;
    };

    if (!state.composed || state.logicalWidth != logicalWidth || state.logicalHeight != logicalHeight) {
        composeFrame();
    }

    bool changed = false;
    if (updateRequested) {
        composeFrame();
        changed = true;
    }

    changed = detail::dslRuntime().update(window, deltaSeconds, pointerScale, dpiScale, inputEnabled) || changed;
    if (detail::dslRuntime().composeRequested()) {
        composeFrame();
        changed = detail::dslRuntime().update(window, 0.0f, pointerScale, dpiScale, inputEnabled) || changed;
        changed = true;
    }

    return changed;
}

inline bool isAnimating() {


    return detail::dslRuntime().isAnimating();
}

inline void render(int windowWidth, int windowHeight, float dpiScale) {


    if (windowWidth <= 0 || windowHeight <= 0 || dpiScale <= 0.0f) {
        return;
    }

    const core::Color clearColor = dslAppConfig().clearColorValue;
    detail::dslRuntime().render(windowWidth, windowHeight, dpiScale, clearColor);
}

inline void releaseGraphicsResources() {


    detail::dslRuntime().releaseGraphicsResources();
}

inline void shutdown() {


    core::async::shutdown();
    detail::dslRuntime().shutdown();
    eui::network::shutdown();
}

} // namespace app
