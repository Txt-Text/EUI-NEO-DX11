#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

#include <glad/glad.h>
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#if defined(_WIN32) || defined(__APPLE__)
#include <SDL_syswm.h>
#endif

#include "eui/app.h"
#include "core/app/app_runner.h"
#include "core/app/dsl_window_manager.h"
#include "core/app/dsl_window_runtime.h"
#include "core/app/main_window_runtime.h"
#include "core/platform/event.h"
#include "core/platform/platform.h"
#include "core/platform/native_bridge.h"
#include "core/render/opengl/opengl_backend.h"
#include "core/platform/window_backend.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace {

struct WindowState : app::AppRunner {
    bool running = true;
};

struct ManagedWindow {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    bool closeRequested = false;
    SDL_Window* parentWindow = nullptr;
    app::DslWindowRuntime content;
};

struct TimerResolutionGuard {
    TimerResolutionGuard() {
#ifdef _WIN32
        timeBeginPeriod(1);
#endif
    }

    ~TimerResolutionGuard() {
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }
};

float pointerScale(SDL_Window* window) {
    int windowWidth = 0;
    int windowHeight = 0;
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    if (windowWidth <= 0 || windowHeight <= 0) {
        return 1.0f;
    }
    const float scaleX = static_cast<float>(drawableWidth) / static_cast<float>(windowWidth);
    const float scaleY = static_cast<float>(drawableHeight) / static_cast<float>(windowHeight);
    return (scaleX + scaleY) * 0.5f;
}

double refreshRate(SDL_Window* window) {
    const int display = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode{};
    if (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 0) {
        return static_cast<double>(mode.refresh_rate);
    }
    return 60.0;
}

void* nativeWindowHandle(SDL_Window* window) {
#if defined(_WIN32) || defined(__APPLE__)
    if (window == nullptr) {
        return nullptr;
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) != SDL_TRUE) {
        return nullptr;
    }
#if defined(_WIN32)
    if (info.subsystem != SDL_SYSWM_WINDOWS) {
        return nullptr;
    }
    return info.info.win.window;
#elif defined(__APPLE__)
    return info.info.cocoa.window;
#endif
#else
    (void)window;
    return nullptr;
#endif
}

float dpiScale(SDL_Window* window) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(nativeWindowHandle(window));
    if (hwnd != nullptr) {
        const UINT dpi = GetDpiForWindow(hwnd);
        if (dpi > 0) {
            return static_cast<float>(dpi) / 96.0f;
        }
    }
#endif
    return pointerScale(window);
}

void attachNativeChildWindow(SDL_Window* parentWindow, SDL_Window* childWindow) {
    eui_set_native_child_window(nativeWindowHandle(parentWindow), nativeWindowHandle(childWindow), 1);
}

void detachNativeChildWindow(SDL_Window* parentWindow, SDL_Window* childWindow) {
    eui_set_native_child_window(nativeWindowHandle(parentWindow), nativeWindowHandle(childWindow), 0);
}

void updateFrameInterval(SDL_Window* window, WindowState& state) {
    state.updateFrameInterval(refreshRate(window), core::window::timeSeconds());
}

bool mapKey(SDL_Keycode key, core::InputKey& mapped) {
    switch (key) {
    case SDLK_BACKSPACE: mapped = core::InputKey::Backspace; return true;
    case SDLK_DELETE: mapped = core::InputKey::Delete; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: mapped = core::InputKey::Enter; return true;
    case SDLK_LEFT: mapped = core::InputKey::Left; return true;
    case SDLK_RIGHT: mapped = core::InputKey::Right; return true;
    case SDLK_UP: mapped = core::InputKey::Up; return true;
    case SDLK_DOWN: mapped = core::InputKey::Down; return true;
    case SDLK_HOME: mapped = core::InputKey::Home; return true;
    case SDLK_END: mapped = core::InputKey::End; return true;
    case SDLK_ESCAPE: mapped = core::InputKey::Escape; return true;
    case SDLK_a: mapped = core::InputKey::A; return true;
    case SDLK_c: mapped = core::InputKey::C; return true;
    case SDLK_v: mapped = core::InputKey::V; return true;
    case SDLK_x: mapped = core::InputKey::X; return true;
    case SDLK_y: mapped = core::InputKey::Y; return true;
    case SDLK_z: mapped = core::InputKey::Z; return true;
    default: return false;
    }
}

void hideToTray(SDL_Window* window, WindowState& state) {
    if (!state.trayAvailable || state.hiddenToTray) {
        return;
    }
    app::releaseGraphicsResources();
    SDL_HideWindow(window);
    state.hiddenToTray = true;
    state.needsRender = false;
}

void restoreFromTray(SDL_Window* window, WindowState& state) {
    if (!state.hiddenToTray) {
        return;
    }
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    state.hiddenToTray = false;
    state.needsRender = true;
}

void requestClose(SDL_Window* window, WindowState& state) {
    if (state.trayAvailable) {
        hideToTray(window, state);
    } else {
        state.running = false;
    }
}

void processMainEvent(SDL_Window* window, WindowState& state, const SDL_Event& event, bool inputEnabled) {
    if (event.type == SDL_QUIT) {
        requestClose(window, state);
        return;
    }
    if (event.type == SDL_TEXTINPUT) {
        if (!inputEnabled) {
            return;
        }
        core::queueTextInput(window, event.text.text);
        state.needsRender = true;
        return;
    }
    if (event.type == SDL_MOUSEWHEEL) {
        if (!inputEnabled) {
            return;
        }
        core::queueScrollInput(window, event.wheel.preciseX, event.wheel.preciseY);
        state.needsRender = true;
        return;
    }
    if (event.type == SDL_KEYDOWN) {
        if (!inputEnabled) {
            return;
        }
        const bool ctrl = (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
        const bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
        core::InputKey key;
        if (mapKey(event.key.keysym.sym, key)) {
            core::queueKeyInput(window, key, ctrl, shift);
            state.needsRender = true;
        }
        if (event.key.keysym.sym == SDLK_ESCAPE && !state.trayAvailable) {
            state.running = false;
        } else if (event.key.keysym.sym == SDLK_ESCAPE) {
            hideToTray(window, state);
        }
        return;
    }
    if (event.type == SDL_WINDOWEVENT) {
        switch (event.window.event) {
        case SDL_WINDOWEVENT_CLOSE:
            requestClose(window, state);
            break;
        case SDL_WINDOWEVENT_MINIMIZED:
            if (state.trayAvailable) {
                hideToTray(window, state);
            }
            break;
        case SDL_WINDOWEVENT_EXPOSED:
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_SHOWN:
        case SDL_WINDOWEVENT_RESTORED:
            state.needsRender = true;
            break;
        default:
            break;
        }
    }
}

std::unique_ptr<ManagedWindow> createManagedWindow(const app::DslWindowRequest& request,
                                                   SDL_Window* parentWindow,
                                                   SDL_GLContext shareContext) {
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, shareContext != nullptr ? 1 : 0);
    SDL_Window* window = SDL_CreateWindow(
        request.title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        request.width,
        request.height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    if (window == nullptr) {
        return {};
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        SDL_DestroyWindow(window);
        return {};
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(0);

    auto managed = std::make_unique<ManagedWindow>();
    managed->window = window;
    managed->context = context;
    managed->parentWindow = parentWindow;
    if (!managed->content.initialize(window, request)) {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        return {};
    }
    if (request.modal) {
        SDL_SetWindowModalFor(window, parentWindow);
        attachNativeChildWindow(parentWindow, window);
        SDL_RaiseWindow(window);
    }
    return managed;
}

void destroyManagedWindow(std::unique_ptr<ManagedWindow>& managed) {
    if (!managed) {
        return;
    }
    if (managed->window != nullptr && managed->context != nullptr) {
        if (managed->content.request().modal && managed->parentWindow != nullptr) {
            detachNativeChildWindow(managed->parentWindow, managed->window);
        }
        SDL_GL_MakeCurrent(managed->window, managed->context);
        core::releaseInputQueue(managed->window);
        managed->content.shutdown(false);
        SDL_GL_DeleteContext(managed->context);
        SDL_DestroyWindow(managed->window);
    }
    managed.reset();
}

bool isManagedWindowClosed(const ManagedWindow& managed) {
    return managed.closeRequested || managed.window == nullptr || managed.context == nullptr;
}

void pruneClosedWindows(app::DslWindowManager<ManagedWindow>& windows) {
    windows.pruneClosed(isManagedWindowClosed, destroyManagedWindow);
}

void createRequestedWindows(app::DslWindowManager<ManagedWindow>& windows,
                            SDL_Window* mainWindow,
                            SDL_GLContext mainContext,
                            const std::vector<app::DslWindowRequest>& requests) {
    SDL_GL_MakeCurrent(mainWindow, mainContext);
    windows.createPending(requests, [&](const app::DslWindowRequest& request) {
        std::unique_ptr<ManagedWindow> managed = createManagedWindow(request, mainWindow, mainContext);
        SDL_GL_MakeCurrent(mainWindow, mainContext);
        return managed;
    });
}

ManagedWindow* findWindow(app::DslWindowManager<ManagedWindow>& windows, Uint32 windowId) {
    return windows.find([&](const ManagedWindow& managed) {
        return managed.window != nullptr && SDL_GetWindowID(managed.window) == windowId;
    });
}

ManagedWindow* findModalWindow(app::DslWindowManager<ManagedWindow>& windows) {
    return windows.modalWindow(isManagedWindowClosed);
}

void processManagedEvent(ManagedWindow& managed, const SDL_Event& event) {
    if (event.type == SDL_TEXTINPUT) {
        core::queueTextInput(managed.window, event.text.text);
        managed.content.markNeedsRender();
        return;
    }
    if (event.type == SDL_MOUSEWHEEL) {
        core::queueScrollInput(managed.window, event.wheel.preciseX, event.wheel.preciseY);
        managed.content.markNeedsRender();
        return;
    }
    if (event.type == SDL_KEYDOWN) {
        const bool ctrl = (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
        const bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
        core::InputKey key;
        if (mapKey(event.key.keysym.sym, key)) {
            core::queueKeyInput(managed.window, key, ctrl, shift);
            managed.content.markNeedsRender();
        }
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            managed.closeRequested = true;
        }
        return;
    }
    if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
            managed.closeRequested = true;
        } else if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                   event.window.event == SDL_WINDOWEVENT_RESIZED ||
                   event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                   event.window.event == SDL_WINDOWEVENT_SHOWN ||
                   event.window.event == SDL_WINDOWEVENT_RESTORED) {
            managed.content.markNeedsRender();
        }
    }
}

bool updateManagedWindow(ManagedWindow& managed, float deltaSeconds, bool externalReady) {
    if (managed.closeRequested || managed.window == nullptr || managed.context == nullptr) {
        return false;
    }

    SDL_GL_MakeCurrent(managed.window, managed.context);
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GL_GetDrawableSize(managed.window, &drawableWidth, &drawableHeight);
    if (drawableWidth <= 0 || drawableHeight <= 0) {
        return true;
    }
    const float dpi = dpiScale(managed.window);
    const float pointer = pointerScale(managed.window);
    const float logicalWidth = static_cast<float>(drawableWidth) / dpi;
    const float logicalHeight = static_cast<float>(drawableHeight) / dpi;

    managed.content.update(managed.window, deltaSeconds, logicalWidth, logicalHeight, pointer, dpi, externalReady);
    if (managed.content.needsRender()) {
        managed.content.render(drawableWidth, drawableHeight, dpi);
        SDL_GL_SwapWindow(managed.window);
    }
    return true;
}

} // namespace

int main() {
    SDL_SetMainReady();
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    SDL_SetHint(SDL_HINT_IME_INTERNAL_EDITING, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        return -1;
    }
    TimerResolutionGuard timerResolution;

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow(
        app::windowTitle(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        app::initialWindowWidth(),
        app::initialWindowHeight(),
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        SDL_Quit();
        return -1;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(0);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    if (!app::initialize(window)) {
        app::shutdown();
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    SDL_StartTextInput();

    WindowState state;
    state.resetTiming(core::window::timeSeconds());
    updateFrameInterval(window, state);
    state.initializeTray();
    core::render::opengl::OpenGLRenderBackend renderBackend(
        [&] {
            SDL_GL_MakeCurrent(window, context);
        },
        [&] {
            SDL_GL_SwapWindow(window);
        });
    app::MainWindowRuntime mainWindowRuntime(state);

    app::DslWindowManager<ManagedWindow> childWindows;
    while (state.running) {
        state.pollTray(false);
        if (state.consumeTrayExitRequested()) {
            break;
        }
        if (state.consumeTrayShowRequested()) {
            restoreFromTray(window, state);
        }
        if (state.hiddenToTray) {
            SDL_Event event{};
            if (SDL_WaitEventTimeout(&event, 100)) {
                processMainEvent(window, state, event, true);
            }
            continue;
        }

        ManagedWindow* modalWindow = findModalWindow(childWindows);
        const bool animating = state.anyAnimating(childWindows.anyAnimating());
        if (!animating) {
            SDL_Event event{};
            if (SDL_WaitEventTimeout(&event, 100)) {
                if (event.type == SDL_WINDOWEVENT || event.type == SDL_KEYDOWN ||
                    event.type == SDL_TEXTINPUT || event.type == SDL_MOUSEWHEEL) {
                    const Uint32 eventWindowId = event.type == SDL_WINDOWEVENT ? event.window.windowID :
                        event.type == SDL_KEYDOWN ? event.key.windowID :
                        event.type == SDL_TEXTINPUT ? event.text.windowID :
                        event.wheel.windowID;
                    if (ManagedWindow* managed = findWindow(childWindows, eventWindowId)) {
                        processManagedEvent(*managed, event);
                    } else {
                        processMainEvent(window, state, event, modalWindow == nullptr);
                    }
                } else {
                    processMainEvent(window, state, event, modalWindow == nullptr);
                }
            }
        } else {
            const double remaining = state.nextFrameTime - core::window::timeSeconds();
            if (remaining > 0.001) {
                std::this_thread::sleep_for(std::chrono::duration<double>(remaining * 0.75));
            }
        }

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_WINDOWEVENT || event.type == SDL_KEYDOWN ||
                event.type == SDL_TEXTINPUT || event.type == SDL_MOUSEWHEEL) {
                const Uint32 eventWindowId = event.type == SDL_WINDOWEVENT ? event.window.windowID :
                    event.type == SDL_KEYDOWN ? event.key.windowID :
                    event.type == SDL_TEXTINPUT ? event.text.windowID :
                    event.wheel.windowID;
                if (ManagedWindow* managed = findWindow(childWindows, eventWindowId)) {
                    processManagedEvent(*managed, event);
                } else {
                    processMainEvent(window, state, event, findModalWindow(childWindows) == nullptr);
                }
            } else {
                processMainEvent(window, state, event, findModalWindow(childWindows) == nullptr);
            }
        }
        pruneClosedWindows(childWindows);
        if (!state.running || state.hiddenToTray) {
            continue;
        }

        const double now = core::window::timeSeconds();

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        if (drawableWidth <= 0 || drawableHeight <= 0) {
            mainWindowRuntime.markUnavailableFrame(core::window::timeSeconds());
            continue;
        }
        const float dpi = dpiScale(window);
        const float pointer = pointerScale(window);
        mainWindowRuntime.runFrame(
            window,
            renderBackend,
            {drawableWidth, drawableHeight, dpi, pointer},
            now,
            refreshRate(window),
            findModalWindow(childWindows) == nullptr,
            [&] {
                createRequestedWindows(childWindows, window, context, app::consumeWindowRequests());
            },
            [&](float frameDelta, bool frameExternalReady) {
                childWindows.updateAll([&](ManagedWindow& managed) {
                    updateManagedWindow(managed, frameDelta, frameExternalReady);
                });
                createRequestedWindows(childWindows, window, context, app::consumeWindowRequests());
            },
            [&](const char* title) {
                SDL_SetWindowTitle(window, title);
            },
            [&] {
                return childWindows.anyAnimating();
            });
    }

    childWindows.destroyAll(destroyManagedWindow);
    core::releaseInputQueue(window);
    core::platform::shutdownTray();
    app::shutdown();
    SDL_StopTextInput();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
