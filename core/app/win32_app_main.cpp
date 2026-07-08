#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <imm.h>
#include <mmsystem.h>

#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "core/app/app_runner.h"
#include "core/app/main_window_runtime.h"
#include "core/input/input_state.h"
#include "core/platform/platform.h"
#include "core/render/render_backend.h"
#include "core/window/window_backend.h"

#include <algorithm>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "imm32.lib")

namespace {

constexpr double kInteractiveResizeRenderInterval = 1.0 / 90.0;
constexpr int kResizeBorderThickness = 8;
constexpr int kTopResizeInsetWhenMaximized = 8;

struct WindowState : app::AppRunner {
    bool running = true;
    bool minimized = false;
    bool interactiveResize = false;
    bool inFrameUpdate = false;
    bool deferredRenderRequested = false;
    wchar_t pendingHighSurrogate = 0;
    double nextResizeRenderTime = 0.0;
    app::MainWindowRuntime* runtime = nullptr;
    core::render::RenderBackend* renderBackend = nullptr;
};

struct TimerResolutionGuard {
    TimerResolutionGuard() {
        timeBeginPeriod(1);
    }

    ~TimerResolutionGuard() {
        timeEndPeriod(1);
    }
};

float dpiScaleForWindow(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

float pointerScaleForWindow(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = std::max(1L, client.right - client.left);
    (void)width;
    return 1.0f;
}

bool mapVirtualKey(WPARAM key, core::InputKey& mapped) {
    switch (key) {
    case VK_BACK: mapped = core::InputKey::Backspace; return true;
    case VK_DELETE: mapped = core::InputKey::Delete; return true;
    case VK_RETURN: mapped = core::InputKey::Enter; return true;
    case VK_LEFT: mapped = core::InputKey::Left; return true;
    case VK_RIGHT: mapped = core::InputKey::Right; return true;
    case VK_UP: mapped = core::InputKey::Up; return true;
    case VK_DOWN: mapped = core::InputKey::Down; return true;
    case VK_HOME: mapped = core::InputKey::Home; return true;
    case VK_END: mapped = core::InputKey::End; return true;
    case VK_ESCAPE: mapped = core::InputKey::Escape; return true;
    case 'A': mapped = core::InputKey::A; return true;
    case 'C': mapped = core::InputKey::C; return true;
    case 'V': mapped = core::InputKey::V; return true;
    case 'X': mapped = core::InputKey::X; return true;
    case 'Y': mapped = core::InputKey::Y; return true;
    case 'Z': mapped = core::InputKey::Z; return true;
    default: return false;
    }
}

bool isHighSurrogate(wchar_t value) {
    return value >= 0xD800 && value <= 0xDBFF;
}

bool isLowSurrogate(wchar_t value) {
    return value >= 0xDC00 && value <= 0xDFFF;
}

void queueUtf16Text(HWND hwnd, const wchar_t* text, int length) {
    if (text == nullptr || length <= 0) {
        return;
    }

    char utf8[8] = {};
    const int utf8Length = WideCharToMultiByte(
        CP_UTF8,
        0,
        text,
        length,
        utf8,
        static_cast<int>(sizeof(utf8)),
        nullptr,
        nullptr);
    if (utf8Length > 0) {
        core::queueTextInput(hwnd, std::string(utf8, utf8 + utf8Length));
    }
}

void queueCompositionText(HWND hwnd) {
    HIMC context = ImmGetContext(hwnd);
    if (context == nullptr) {
        return;
    }

    const LONG bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
    if (bytes > 0) {
        std::wstring wide(static_cast<std::size_t>(bytes / sizeof(wchar_t)), L'\0');
        ImmGetCompositionStringW(context, GCS_COMPSTR, wide.data(), bytes);
        const int utf8Bytes = WideCharToMultiByte(
            CP_UTF8,
            0,
            wide.c_str(),
            static_cast<int>(wide.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        std::string text(static_cast<std::size_t>(std::max(0, utf8Bytes)), '\0');
        if (utf8Bytes > 0) {
            WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.c_str(),
                static_cast<int>(wide.size()),
                text.data(),
                utf8Bytes,
                nullptr,
                nullptr);
        }
        core::queueTextEditing(hwnd, text);
    } else {
        core::queueTextEditing(hwnd, {});
    }

    ImmReleaseContext(hwnd, context);
}

void renderWindowNow(HWND hwnd, WindowState& state, bool force = false) {
    if (state.runtime == nullptr || state.renderBackend == nullptr || state.minimized || !state.running) {
        return;
    }
    if (state.inFrameUpdate) {
        state.deferredRenderRequested = true;
        state.paintRequested = true;
        return;
    }

    const double now = core::window::timeSeconds();
    if (!force && now < state.nextResizeRenderTime) {
        return;
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    const int framebufferWidth = std::max(1L, client.right - client.left);
    const int framebufferHeight = std::max(1L, client.bottom - client.top);
    const float dpiScale = dpiScaleForWindow(hwnd);
    const float pointerScale = pointerScaleForWindow(hwnd);

    core::syncPointerState(hwnd, pointerScale);
    app::detail::cancelPointerInteractions();
    state.runtime->updateAndRender(
        hwnd,
        *state.renderBackend,
        {framebufferWidth, framebufferHeight, dpiScale, pointerScale},
        0.0f,
        true,
        false,
        [] {});
    state.nextResizeRenderTime = now + kInteractiveResizeRenderInterval;
}

bool customTitleBarActive() {
    return app::customTitleBarEnabled();
}

bool isWindowMaximized(HWND hwnd) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    return GetWindowPlacement(hwnd, &placement) && placement.showCmd == SW_MAXIMIZE;
}

int customTitleBarHeightPx(HWND hwnd) {
    return std::max(1, static_cast<int>(std::lround(app::detail::customTitleBarHeight() * dpiScaleForWindow(hwnd))));
}

RECT currentMonitorWorkArea(HWND hwnd) {
    RECT fallback{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback, 0);

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo)) {
        return monitorInfo.rcWork;
    }
    return fallback;
}

void applyMaximizedClientInsets(HWND hwnd, RECT& clientRect) {
    if (!isWindowMaximized(hwnd)) {
        return;
    }

    const RECT workArea = currentMonitorWorkArea(hwnd);
    clientRect.left = workArea.left;
    clientRect.top = workArea.top;
    clientRect.right = workArea.right;
    clientRect.bottom = workArea.bottom;
}

LRESULT hitTestResizeBorder(HWND hwnd, LPARAM lParam) {
    RECT windowRect{};
    if (!GetWindowRect(hwnd, &windowRect)) {
        return HTCLIENT;
    }

    const int x = GET_X_LPARAM(lParam);
    const int y = GET_Y_LPARAM(lParam);
    const bool left = x >= windowRect.left && x < windowRect.left + kResizeBorderThickness;
    const bool right = x < windowRect.right && x >= windowRect.right - kResizeBorderThickness;
    const bool top = !isWindowMaximized(hwnd) && y >= windowRect.top && y < windowRect.top + kResizeBorderThickness;
    const bool bottom = y < windowRect.bottom && y >= windowRect.bottom - kResizeBorderThickness;

    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTCLIENT;
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WindowState* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_NCCALCSIZE:
        if (customTitleBarActive()) {
            if (wParam && lParam != 0) {
                NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                applyMaximizedClientInsets(hwnd, params->rgrc[0]);
                return 0;
            }
            RECT* rect = reinterpret_cast<RECT*>(lParam);
            if (rect != nullptr) {
                applyMaximizedClientInsets(hwnd, *rect);
                return 0;
            }
            return 0;
                }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_NCHITTEST:
        if (customTitleBarActive()) {
            const LRESULT resizeHit = hitTestResizeBorder(hwnd, lParam);
            if (resizeHit != HTCLIENT) {
                return resizeHit;
            }

            RECT windowRect{};
            if (GetWindowRect(hwnd, &windowRect)) {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                const int titleBarHeight = customTitleBarHeightPx(hwnd);
                const int topInset = isWindowMaximized(hwnd) ? kTopResizeInsetWhenMaximized : 0;
                if (y >= windowRect.top + topInset && y < windowRect.top + titleBarHeight) {
                    return HTCLIENT;
                }
            }
            return HTCLIENT;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_GETMINMAXINFO:
        if (customTitleBarActive()) {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            if (info != nullptr) {
                MONITORINFO monitorInfo{};
                monitorInfo.cbSize = sizeof(monitorInfo);
                const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                if (monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo)) {
                    const RECT& workArea = monitorInfo.rcWork;
                    const RECT& monitorArea = monitorInfo.rcMonitor;
                    info->ptMaxPosition.x = workArea.left - monitorArea.left;
                    info->ptMaxPosition.y = workArea.top - monitorArea.top;
                    info->ptMaxSize.x = workArea.right - workArea.left;
                    info->ptMaxSize.y = workArea.bottom - workArea.top;
                }
            }
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_CLOSE:
        if (state) {
            state->running = false;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (state) {
            state->minimized = wParam == SIZE_MINIMIZED;
            state->resetTiming(core::window::timeSeconds());
            if (!state->minimized) {
                state->paintRequested = true;
                if (state->interactiveResize) {
                    renderWindowNow(hwnd, *state, false);
                } else {
                    state->deferredRenderRequested = true;
                }
            }
        }
        return 0;
    case WM_DPICHANGED:
        if (state) {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) {
                SetWindowPos(hwnd,
                             nullptr,
                             suggested->left,
                             suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            state->resetTiming(core::window::timeSeconds());
            state->paintRequested = true;
            state->deferredRenderRequested = true;
        }
        return 0;
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        core::window::refreshWindowTheme(hwnd);
        if (state) {
            state->paintRequested = true;
            state->deferredRenderRequested = true;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_ENTERSIZEMOVE:
        if (state) {
            state->interactiveResize = true;
            core::syncPointerState(hwnd, pointerScaleForWindow(hwnd));
            state->resetTiming(core::window::timeSeconds());
            state->nextResizeRenderTime = 0.0;
        }
        return 0;
    case WM_EXITSIZEMOVE:
        if (state) {
            state->interactiveResize = false;
            core::syncPointerState(hwnd, pointerScaleForWindow(hwnd));
            app::detail::cancelPointerInteractions();
            state->resetTiming(core::window::timeSeconds());
            state->nextResizeRenderTime = 0.0;
            state->paintRequested = true;
            state->deferredRenderRequested = true;
        }
        return 0;
    case WM_MOUSEWHEEL:
        core::queueScrollInput(
            hwnd,
            0.0,
            static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<double>(WHEEL_DELTA));
        if (state) {
            state->paintRequested = true;
        }
        return 0;
    case WM_CHAR: {
        if (state == nullptr) {
            return 0;
            }

        const wchar_t value = static_cast<wchar_t>(wParam);
        if (isHighSurrogate(value)) {
            state->pendingHighSurrogate = value;
            return 0;
            }

        if (isLowSurrogate(value) && state->pendingHighSurrogate != 0) {
            const wchar_t pair[2] = {state->pendingHighSurrogate, value};
            state->pendingHighSurrogate = 0;
            queueUtf16Text(hwnd, pair, 2);
            state->paintRequested = true;
            return 0;
        }

        state->pendingHighSurrogate = 0;
        queueUtf16Text(hwnd, &value, 1);
        state->paintRequested = true;
        return 0;
    }
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
        queueCompositionText(hwnd);
        if (state) {
            state->paintRequested = true;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_IME_ENDCOMPOSITION:
        core::queueTextEditing(hwnd, {});
        if (state) {
            state->paintRequested = true;
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (state) {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            core::InputKey mapped{};
            if (mapVirtualKey(wParam, mapped)) {
                core::queueKeyInput(hwnd, mapped, ctrl, shift);
                state->paintRequested = true;
            }
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace

int eui_win32_entry() {
    core::platform::repairCurrentWorkingDirectory();
    core::render::initializeRenderBackendLoader();
    TimerResolutionGuard timerResolution;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    core::window::WindowCreateRequest request;
    request.width = app::initialWindowWidth();
    request.height = app::initialWindowHeight();
    request.title = app::windowTitle();
    request.renderApi = core::render::windowRenderApi();

    HWND window = static_cast<HWND>(core::window::createWindow(request));
    if (window == nullptr) {
        return -1;
    }

    WindowState state;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MainWindowProc));
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    auto renderBackend = core::render::createRenderBackend(window);
    if (!renderBackend || !renderBackend->initialize()) {
        core::window::destroyWindow(window);
        return -1;
    }
    state.renderBackend = renderBackend.get();

    if (!app::initialize(window)) {
        app::shutdown();
        renderBackend.reset();
        core::window::destroyWindow(window);
        return -1;
    }

    app::MainWindowRuntime mainWindowRuntime(state);
    state.runtime = &mainWindowRuntime;
    state.resetTiming(core::window::timeSeconds());
    state.updateFrameInterval(60.0, state.lastTitleUpdate, true);

    while (state.running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                state.running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!state.running) {
            break;
        }

        if (state.minimized) {
            WaitMessage();
            mainWindowRuntime.markUnavailableFrame(core::window::timeSeconds());
            continue;
        }

        RECT client{};
        GetClientRect(window, &client);
        const int framebufferWidth = std::max(1L, client.right - client.left);
        const int framebufferHeight = std::max(1L, client.bottom - client.top);
        const float dpiScale = dpiScaleForWindow(window);
        const float pointerScale = pointerScaleForWindow(window);

        state.inFrameUpdate = true;
        mainWindowRuntime.runFrame(
            window,
            *renderBackend,
            {framebufferWidth, framebufferHeight, dpiScale, pointerScale},
            core::window::timeSeconds(),
            state.interactiveResize ? 90.0 : 60.0,
            !state.interactiveResize,
            [] {},
            [&](float, bool) {},
            [&](const char* title) {
                SetWindowTextA(window, title);
            },
            [] {
                return false;
            });
        state.inFrameUpdate = false;

        if (state.deferredRenderRequested && state.running && !state.minimized) {
            state.deferredRenderRequested = false;
            renderWindowNow(window, state, true);
        }

        if (!state.paintRequested && !app::isAnimating() && !core::hasPendingPointerInput(window, pointerScale)) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }

    core::releaseInputQueue(window);
    renderBackend->makeCurrent();
    renderBackend->releaseRenderCache();
    {
        core::render::ScopedRenderBackend scopedRenderBackend(*renderBackend);
        app::shutdown();
    }
    renderBackend.reset();
    state.renderBackend = nullptr;
    state.runtime = nullptr;
    core::window::destroyWindow(window);
    return 0;
}

